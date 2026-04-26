# MAC Protocol Implementation Checklist

## Decision Checklist

- Protocol name / paper name:
- Target benchmark `method`:
- Implementation choice:
  - `DynamicTDMA` + new `macMode`
  - new `.cc/.ned` module
- Network:
  - `TDMANetwork`
  - new network
- Runner:
  - `ablation`
  - `omnet_only`
  - `omnet_custom`
  - `external`
- Adapter:
  - `dynamic_tdma`
  - `canonical`
  - custom adapter
- Needs PPO/RL:
  - `true`
  - `false`

## Method Registration Template

```text
method	implementation	network	runner	macMode	adapter	needs_rl	description
plain_tdma	DynamicTDMA	TDMANetwork	omnet_only	plain_tdma	canonical	false	ordinary TDMA owner-slot baseline
zmac_like	DynamicTDMA	TDMANetwork	omnet_only	zmac_like	canonical	false	Z-MAC-inspired owner priority and slot stealing baseline
trama_like	DynamicTDMA	TDMANetwork	omnet_only	trama_like	canonical	false	TRAMA-inspired traffic-adaptive election baseline
```

## Canonical Metrics

Use these fields for cross-protocol comparison:

```text
scenario
method
seed
sim_time_s
packets_generated
packets_delivered
packets_dropped
goodput_per_second
packet_delivery_ratio
drop_or_backlog_rate
mean_delay_s
p95_delay_s
jain_fairness
starvation_ratio
control_overhead_ratio
channel_busy_ratio
energy_proxy
convergence_time_s
complete
```

TDMA-specific fields may be reported as secondary:

```text
frames
data_slots
packets_per_frame
goodput_per_slot
slot_util_mean
spatial_reuse_gain
mac_efficiency
```

## Smoke Validation Template

```bash
bash -n scripts/benchmark_suite.sh
python -m py_compile scripts/summarize_benchmark.py
./scripts/benchmark_suite.sh \
  --suite mac_protocol_smoke \
  --scenarios "N9_ring" \
  --methods "baseline <new_method>" \
  --seeds "1" \
  --sim_time 300 \
  --jobs 2 \
  --metrics_mode summary
```

## Documentation Requirements

- State whether the method is a full protocol reproduction, a paper-inspired baseline, or an imported external reference.
- State whether it shares `DynamicTDMA.cc` or uses a separate OMNeT++ module.
- State which metrics are canonical and which are TDMA-specific secondary metrics.
- State known limitations, especially missing energy or sleep-state models.
