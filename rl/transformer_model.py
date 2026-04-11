from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

import torch
from torch import nn


@dataclass
class FeatureConfig:
    num_slots: int
    numeric_keys: Tuple[str, ...] = (
        "Cctrl",
        "mu_nbr",
        "Qt",
        "lambda_ewma",
        "Wt",
        "Sharet",
        "Share_avgnbr",
        "Jlocal",
        "Envy",
        "Pt_1",
        "Hcoll",
    )


def _parse_bown(bown: str, num_slots: int) -> List[float]:
    bits = [1.0 if ch == "1" else 0.0 for ch in bown.strip()]
    if len(bits) < num_slots:
        bits.extend([0.0] * (num_slots - len(bits)))
    return bits[:num_slots]


def _parse_t2hop(t2hop: str, num_slots: int) -> List[float]:
    # For each slot: [occupied_flag, min_hop_norm]
    occupied = [0.0] * num_slots
    min_hop = [0.0] * num_slots
    entries = [e for e in t2hop.split(";") if e]
    for entry in entries:
        if ":" not in entry:
            continue
        slot_str, payload = entry.split(":", 1)
        if not slot_str.startswith("s"):
            continue
        try:
            slot_idx = int(slot_str[1:])
        except ValueError:
            continue
        if slot_idx < 0 or slot_idx >= num_slots:
            continue
        payload = payload.strip()
        if payload == "free":
            occupied[slot_idx] = 0.0
            min_hop[slot_idx] = 0.0
            continue
        hop_vals: List[int] = []
        for token in payload.split("|"):
            token = token.strip()
            hop_pos = token.find("(h")
            if hop_pos == -1:
                continue
            hop_end = token.find(")", hop_pos)
            if hop_end == -1:
                continue
            hop_str = token[hop_pos + 2 : hop_end]
            try:
                hop_vals.append(int(hop_str))
            except ValueError:
                continue
        if hop_vals:
            occupied[slot_idx] = 1.0
            min_hop_val = min(hop_vals)
            # Normalize hop to [0, 1], assume hop in [0, 3]
            min_hop[slot_idx] = max(0.0, min(1.0, min_hop_val / 3.0))
        else:
            occupied[slot_idx] = 1.0
            min_hop[slot_idx] = 1.0
    features: List[float] = []
    for i in range(num_slots):
        features.append(occupied[i])
        features.append(min_hop[i])
    return features


class FeatureExtractor:
    def __init__(self, cfg: FeatureConfig):
        self.cfg = cfg

    def __call__(self, record: Dict[str, object]) -> torch.Tensor:
        bown = _parse_bown(str(record.get("Bown", "")), self.cfg.num_slots)
        t2hop = _parse_t2hop(str(record.get("T2hop", "")), self.cfg.num_slots)
        numeric = [
            float(record.get(k, 0.0)) for k in self.cfg.numeric_keys
        ]
        feats = numeric + bown + t2hop
        return torch.tensor(feats, dtype=torch.float32)


class JsonlSequenceDataset(torch.utils.data.Dataset):
    """
    Builds sequences of frame features for one node.
    Returns (x, y) where y is the next-frame value of target_key.
    """

    def __init__(
        self,
        jsonl_path: Path,
        cfg: FeatureConfig,
        seq_len: int = 8,
        target_key: str = "Qt",
    ):
        self.cfg = cfg
        self.seq_len = seq_len
        self.target_key = target_key
        self.extractor = FeatureExtractor(cfg)

        self.records = self._load(jsonl_path)
        self.features = [self.extractor(r) for r in self.records]
        self.targets = [float(r.get(target_key, 0.0)) for r in self.records]

    def _load(self, path: Path) -> List[Dict[str, object]]:
        records: List[Dict[str, object]] = []
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                records.append(json.loads(line))
        return records

    def __len__(self) -> int:
        return max(0, len(self.features) - self.seq_len)

    def __getitem__(self, idx: int) -> Tuple[torch.Tensor, torch.Tensor]:
        x = torch.stack(self.features[idx : idx + self.seq_len], dim=0)
        y = torch.tensor(self.targets[idx + self.seq_len], dtype=torch.float32)
        return x, y


class TransformerRegressor(nn.Module):
    def __init__(
        self,
        input_dim: int,
        d_model: int = 128,
        nhead: int = 4,
        num_layers: int = 3,
        dim_feedforward: int = 256,
        dropout: float = 0.1,
    ):
        super().__init__()
        self.input_proj = nn.Linear(input_dim, d_model)
        self.pos_embed = nn.Parameter(torch.zeros(1, 512, d_model))
        encoder_layer = nn.TransformerEncoderLayer(
            d_model=d_model,
            nhead=nhead,
            dim_feedforward=dim_feedforward,
            dropout=dropout,
            batch_first=True,
        )
        self.encoder = nn.TransformerEncoder(encoder_layer, num_layers=num_layers)
        self.head = nn.Sequential(
            nn.LayerNorm(d_model),
            nn.Linear(d_model, d_model),
            nn.ReLU(),
            nn.Linear(d_model, 1),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: [B, T, F]
        b, t, _ = x.shape
        x = self.input_proj(x)
        x = x + self.pos_embed[:, :t, :]
        h = self.encoder(x)
        out = self.head(h[:, -1, :])
        return out.squeeze(-1)


def build_model(cfg: FeatureConfig, seq_len: int) -> TransformerRegressor:
    dummy = FeatureExtractor(cfg)(
        {"Bown": "0" * cfg.num_slots, "T2hop": "", **{k: 0 for k in cfg.numeric_keys}}
    )
    input_dim = int(dummy.numel())
    model = TransformerRegressor(input_dim=input_dim)
    return model

