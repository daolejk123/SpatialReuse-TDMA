//
// Generated file, do not edit! Created by opp_msgtool 6.3 from TDMA_Messages.msg.
//

// Disable warnings about unused variables, empty switch stmts, etc:
#ifdef _MSC_VER
#  pragma warning(disable:4101)
#  pragma warning(disable:4065)
#endif

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wshadow"
#  pragma clang diagnostic ignored "-Wconversion"
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wc++98-compat"
#  pragma clang diagnostic ignored "-Wunreachable-code-break"
#  pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wold-style-cast"
#  pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#  pragma GCC diagnostic ignored "-Wfloat-conversion"
#endif

#include <iostream>
#include <sstream>
#include <memory>
#include <type_traits>
#include "TDMA_Messages_m.h"

namespace omnetpp {

// Template pack/unpack rules. They are declared *after* a1l type-specific pack functions for multiple reasons.
// They are in the omnetpp namespace, to allow them to be found by argument-dependent lookup via the cCommBuffer argument

// Packing/unpacking an std::vector
template<typename T, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::vector<T,A>& v)
{
    int n = v.size();
    doParsimPacking(buffer, n);
    for (int i = 0; i < n; i++)
        doParsimPacking(buffer, v[i]);
}

template<typename T, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::vector<T,A>& v)
{
    int n;
    doParsimUnpacking(buffer, n);
    v.resize(n);
    for (int i = 0; i < n; i++)
        doParsimUnpacking(buffer, v[i]);
}

// Packing/unpacking an std::list
template<typename T, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::list<T,A>& l)
{
    doParsimPacking(buffer, (int)l.size());
    for (typename std::list<T,A>::const_iterator it = l.begin(); it != l.end(); ++it)
        doParsimPacking(buffer, (T&)*it);
}

template<typename T, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::list<T,A>& l)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        l.push_back(T());
        doParsimUnpacking(buffer, l.back());
    }
}

// Packing/unpacking an std::set
template<typename T, typename Tr, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::set<T,Tr,A>& s)
{
    doParsimPacking(buffer, (int)s.size());
    for (typename std::set<T,Tr,A>::const_iterator it = s.begin(); it != s.end(); ++it)
        doParsimPacking(buffer, *it);
}

template<typename T, typename Tr, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::set<T,Tr,A>& s)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        T x;
        doParsimUnpacking(buffer, x);
        s.insert(x);
    }
}

// Packing/unpacking an std::map
template<typename K, typename V, typename Tr, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::map<K,V,Tr,A>& m)
{
    doParsimPacking(buffer, (int)m.size());
    for (typename std::map<K,V,Tr,A>::const_iterator it = m.begin(); it != m.end(); ++it) {
        doParsimPacking(buffer, it->first);
        doParsimPacking(buffer, it->second);
    }
}

template<typename K, typename V, typename Tr, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::map<K,V,Tr,A>& m)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        K k; V v;
        doParsimUnpacking(buffer, k);
        doParsimUnpacking(buffer, v);
        m[k] = v;
    }
}

// Default pack/unpack function for arrays
template<typename T>
void doParsimArrayPacking(omnetpp::cCommBuffer *b, const T *t, int n)
{
    for (int i = 0; i < n; i++)
        doParsimPacking(b, t[i]);
}

template<typename T>
void doParsimArrayUnpacking(omnetpp::cCommBuffer *b, T *t, int n)
{
    for (int i = 0; i < n; i++)
        doParsimUnpacking(b, t[i]);
}

// Default rule to prevent compiler from choosing base class' doParsimPacking() function
template<typename T>
void doParsimPacking(omnetpp::cCommBuffer *, const T& t)
{
    throw omnetpp::cRuntimeError("Parsim error: No doParsimPacking() function for type %s", omnetpp::opp_typename(typeid(t)));
}

template<typename T>
void doParsimUnpacking(omnetpp::cCommBuffer *, T& t)
{
    throw omnetpp::cRuntimeError("Parsim error: No doParsimUnpacking() function for type %s", omnetpp::opp_typename(typeid(t)));
}

}  // namespace omnetpp

Register_Class(TDMADataPacket)

TDMADataPacket::TDMADataPacket(const char *name, short kind) : ::omnetpp::cPacket(name, kind)
{
}

TDMADataPacket::TDMADataPacket(const TDMADataPacket& other) : ::omnetpp::cPacket(other)
{
    copy(other);
}

TDMADataPacket::~TDMADataPacket()
{
}

TDMADataPacket& TDMADataPacket::operator=(const TDMADataPacket& other)
{
    if (this == &other) return *this;
    ::omnetpp::cPacket::operator=(other);
    copy(other);
    return *this;
}

void TDMADataPacket::copy(const TDMADataPacket& other)
{
    this->srcId = other.srcId;
    this->destId = other.destId;
    this->payload = other.payload;
    this->releasedSlotIndex = other.releasedSlotIndex;
}

void TDMADataPacket::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::omnetpp::cPacket::parsimPack(b);
    doParsimPacking(b,this->srcId);
    doParsimPacking(b,this->destId);
    doParsimPacking(b,this->payload);
    doParsimPacking(b,this->releasedSlotIndex);
}

void TDMADataPacket::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::omnetpp::cPacket::parsimUnpack(b);
    doParsimUnpacking(b,this->srcId);
    doParsimUnpacking(b,this->destId);
    doParsimUnpacking(b,this->payload);
    doParsimUnpacking(b,this->releasedSlotIndex);
}

int TDMADataPacket::getSrcId() const
{
    return this->srcId;
}

void TDMADataPacket::setSrcId(int srcId)
{
    this->srcId = srcId;
}

int TDMADataPacket::getDestId() const
{
    return this->destId;
}

void TDMADataPacket::setDestId(int destId)
{
    this->destId = destId;
}

const char * TDMADataPacket::getPayload() const
{
    return this->payload.c_str();
}

void TDMADataPacket::setPayload(const char * payload)
{
    this->payload = payload;
}

int TDMADataPacket::getReleasedSlotIndex() const
{
    return this->releasedSlotIndex;
}

void TDMADataPacket::setReleasedSlotIndex(int releasedSlotIndex)
{
    this->releasedSlotIndex = releasedSlotIndex;
}

class TDMADataPacketDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_srcId,
        FIELD_destId,
        FIELD_payload,
        FIELD_releasedSlotIndex,
    };
  public:
    TDMADataPacketDescriptor();
    virtual ~TDMADataPacketDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TDMADataPacketDescriptor)

TDMADataPacketDescriptor::TDMADataPacketDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(TDMADataPacket)), "omnetpp::cPacket")
{
    propertyNames = nullptr;
}

TDMADataPacketDescriptor::~TDMADataPacketDescriptor()
{
    delete[] propertyNames;
}

bool TDMADataPacketDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TDMADataPacket *>(obj)!=nullptr;
}

const char **TDMADataPacketDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TDMADataPacketDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TDMADataPacketDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 4+base->getFieldCount() : 4;
}

unsigned int TDMADataPacketDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_srcId
        FD_ISEDITABLE,    // FIELD_destId
        FD_ISEDITABLE,    // FIELD_payload
        FD_ISEDITABLE,    // FIELD_releasedSlotIndex
    };
    return (field >= 0 && field < 4) ? fieldTypeFlags[field] : 0;
}

const char *TDMADataPacketDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "srcId",
        "destId",
        "payload",
        "releasedSlotIndex",
    };
    return (field >= 0 && field < 4) ? fieldNames[field] : nullptr;
}

int TDMADataPacketDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "srcId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "destId") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "payload") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "releasedSlotIndex") == 0) return baseIndex + 3;
    return base ? base->findField(fieldName) : -1;
}

const char *TDMADataPacketDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_srcId
        "int",    // FIELD_destId
        "string",    // FIELD_payload
        "int",    // FIELD_releasedSlotIndex
    };
    return (field >= 0 && field < 4) ? fieldTypeStrings[field] : nullptr;
}

const char **TDMADataPacketDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TDMADataPacketDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TDMADataPacketDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void TDMADataPacketDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TDMADataPacket'", field);
    }
}

const char *TDMADataPacketDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TDMADataPacketDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: return long2string(pp->getSrcId());
        case FIELD_destId: return long2string(pp->getDestId());
        case FIELD_payload: return oppstring2string(pp->getPayload());
        case FIELD_releasedSlotIndex: return long2string(pp->getReleasedSlotIndex());
        default: return "";
    }
}

void TDMADataPacketDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: pp->setSrcId(string2long(value)); break;
        case FIELD_destId: pp->setDestId(string2long(value)); break;
        case FIELD_payload: pp->setPayload((value)); break;
        case FIELD_releasedSlotIndex: pp->setReleasedSlotIndex(string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMADataPacket'", field);
    }
}

omnetpp::cValue TDMADataPacketDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: return pp->getSrcId();
        case FIELD_destId: return pp->getDestId();
        case FIELD_payload: return pp->getPayload();
        case FIELD_releasedSlotIndex: return pp->getReleasedSlotIndex();
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TDMADataPacket' as cValue -- field index out of range?", field);
    }
}

void TDMADataPacketDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: pp->setSrcId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_destId: pp->setDestId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_payload: pp->setPayload(value.stringValue()); break;
        case FIELD_releasedSlotIndex: pp->setReleasedSlotIndex(omnetpp::checked_int_cast<int>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMADataPacket'", field);
    }
}

const char *TDMADataPacketDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TDMADataPacketDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TDMADataPacketDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMADataPacket *pp = omnetpp::fromAnyPtr<TDMADataPacket>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMADataPacket'", field);
    }
}

Register_Class(TDMAGrantRequest)

TDMAGrantRequest::TDMAGrantRequest(const char *name, short kind) : ::omnetpp::cPacket(name, kind)
{
}

TDMAGrantRequest::TDMAGrantRequest(const TDMAGrantRequest& other) : ::omnetpp::cPacket(other)
{
    copy(other);
}

TDMAGrantRequest::~TDMAGrantRequest()
{
    delete [] this->targetNodeIds;
    delete [] this->priorities;
    delete [] this->occupancyInfo;
    delete [] this->occupancyHops;
}

TDMAGrantRequest& TDMAGrantRequest::operator=(const TDMAGrantRequest& other)
{
    if (this == &other) return *this;
    ::omnetpp::cPacket::operator=(other);
    copy(other);
    return *this;
}

void TDMAGrantRequest::copy(const TDMAGrantRequest& other)
{
    this->srcId = other.srcId;
    delete [] this->targetNodeIds;
    this->targetNodeIds = (other.targetNodeIds_arraysize==0) ? nullptr : new int[other.targetNodeIds_arraysize];
    targetNodeIds_arraysize = other.targetNodeIds_arraysize;
    for (size_t i = 0; i < targetNodeIds_arraysize; i++) {
        this->targetNodeIds[i] = other.targetNodeIds[i];
    }
    delete [] this->priorities;
    this->priorities = (other.priorities_arraysize==0) ? nullptr : new double[other.priorities_arraysize];
    priorities_arraysize = other.priorities_arraysize;
    for (size_t i = 0; i < priorities_arraysize; i++) {
        this->priorities[i] = other.priorities[i];
    }
    delete [] this->occupancyInfo;
    this->occupancyInfo = (other.occupancyInfo_arraysize==0) ? nullptr : new int[other.occupancyInfo_arraysize];
    occupancyInfo_arraysize = other.occupancyInfo_arraysize;
    for (size_t i = 0; i < occupancyInfo_arraysize; i++) {
        this->occupancyInfo[i] = other.occupancyInfo[i];
    }
    delete [] this->occupancyHops;
    this->occupancyHops = (other.occupancyHops_arraysize==0) ? nullptr : new int[other.occupancyHops_arraysize];
    occupancyHops_arraysize = other.occupancyHops_arraysize;
    for (size_t i = 0; i < occupancyHops_arraysize; i++) {
        this->occupancyHops[i] = other.occupancyHops[i];
    }
}

void TDMAGrantRequest::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::omnetpp::cPacket::parsimPack(b);
    doParsimPacking(b,this->srcId);
    b->pack(targetNodeIds_arraysize);
    doParsimArrayPacking(b,this->targetNodeIds,targetNodeIds_arraysize);
    b->pack(priorities_arraysize);
    doParsimArrayPacking(b,this->priorities,priorities_arraysize);
    b->pack(occupancyInfo_arraysize);
    doParsimArrayPacking(b,this->occupancyInfo,occupancyInfo_arraysize);
    b->pack(occupancyHops_arraysize);
    doParsimArrayPacking(b,this->occupancyHops,occupancyHops_arraysize);
}

void TDMAGrantRequest::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::omnetpp::cPacket::parsimUnpack(b);
    doParsimUnpacking(b,this->srcId);
    delete [] this->targetNodeIds;
    b->unpack(targetNodeIds_arraysize);
    if (targetNodeIds_arraysize == 0) {
        this->targetNodeIds = nullptr;
    } else {
        this->targetNodeIds = new int[targetNodeIds_arraysize];
        doParsimArrayUnpacking(b,this->targetNodeIds,targetNodeIds_arraysize);
    }
    delete [] this->priorities;
    b->unpack(priorities_arraysize);
    if (priorities_arraysize == 0) {
        this->priorities = nullptr;
    } else {
        this->priorities = new double[priorities_arraysize];
        doParsimArrayUnpacking(b,this->priorities,priorities_arraysize);
    }
    delete [] this->occupancyInfo;
    b->unpack(occupancyInfo_arraysize);
    if (occupancyInfo_arraysize == 0) {
        this->occupancyInfo = nullptr;
    } else {
        this->occupancyInfo = new int[occupancyInfo_arraysize];
        doParsimArrayUnpacking(b,this->occupancyInfo,occupancyInfo_arraysize);
    }
    delete [] this->occupancyHops;
    b->unpack(occupancyHops_arraysize);
    if (occupancyHops_arraysize == 0) {
        this->occupancyHops = nullptr;
    } else {
        this->occupancyHops = new int[occupancyHops_arraysize];
        doParsimArrayUnpacking(b,this->occupancyHops,occupancyHops_arraysize);
    }
}

int TDMAGrantRequest::getSrcId() const
{
    return this->srcId;
}

void TDMAGrantRequest::setSrcId(int srcId)
{
    this->srcId = srcId;
}

size_t TDMAGrantRequest::getTargetNodeIdsArraySize() const
{
    return targetNodeIds_arraysize;
}

int TDMAGrantRequest::getTargetNodeIds(size_t k) const
{
    if (k >= targetNodeIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)targetNodeIds_arraysize, (unsigned long)k);
    return this->targetNodeIds[k];
}

void TDMAGrantRequest::setTargetNodeIdsArraySize(size_t newSize)
{
    int *targetNodeIds2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = targetNodeIds_arraysize < newSize ? targetNodeIds_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        targetNodeIds2[i] = this->targetNodeIds[i];
    for (size_t i = minSize; i < newSize; i++)
        targetNodeIds2[i] = 0;
    delete [] this->targetNodeIds;
    this->targetNodeIds = targetNodeIds2;
    targetNodeIds_arraysize = newSize;
}

void TDMAGrantRequest::setTargetNodeIds(size_t k, int targetNodeIds)
{
    if (k >= targetNodeIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)targetNodeIds_arraysize, (unsigned long)k);
    this->targetNodeIds[k] = targetNodeIds;
}

void TDMAGrantRequest::insertTargetNodeIds(size_t k, int targetNodeIds)
{
    if (k > targetNodeIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)targetNodeIds_arraysize, (unsigned long)k);
    size_t newSize = targetNodeIds_arraysize + 1;
    int *targetNodeIds2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        targetNodeIds2[i] = this->targetNodeIds[i];
    targetNodeIds2[k] = targetNodeIds;
    for (i = k + 1; i < newSize; i++)
        targetNodeIds2[i] = this->targetNodeIds[i-1];
    delete [] this->targetNodeIds;
    this->targetNodeIds = targetNodeIds2;
    targetNodeIds_arraysize = newSize;
}

void TDMAGrantRequest::appendTargetNodeIds(int targetNodeIds)
{
    insertTargetNodeIds(targetNodeIds_arraysize, targetNodeIds);
}

void TDMAGrantRequest::eraseTargetNodeIds(size_t k)
{
    if (k >= targetNodeIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)targetNodeIds_arraysize, (unsigned long)k);
    size_t newSize = targetNodeIds_arraysize - 1;
    int *targetNodeIds2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        targetNodeIds2[i] = this->targetNodeIds[i];
    for (i = k; i < newSize; i++)
        targetNodeIds2[i] = this->targetNodeIds[i+1];
    delete [] this->targetNodeIds;
    this->targetNodeIds = targetNodeIds2;
    targetNodeIds_arraysize = newSize;
}

size_t TDMAGrantRequest::getPrioritiesArraySize() const
{
    return priorities_arraysize;
}

double TDMAGrantRequest::getPriorities(size_t k) const
{
    if (k >= priorities_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)priorities_arraysize, (unsigned long)k);
    return this->priorities[k];
}

void TDMAGrantRequest::setPrioritiesArraySize(size_t newSize)
{
    double *priorities2 = (newSize==0) ? nullptr : new double[newSize];
    size_t minSize = priorities_arraysize < newSize ? priorities_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        priorities2[i] = this->priorities[i];
    for (size_t i = minSize; i < newSize; i++)
        priorities2[i] = 0;
    delete [] this->priorities;
    this->priorities = priorities2;
    priorities_arraysize = newSize;
}

void TDMAGrantRequest::setPriorities(size_t k, double priorities)
{
    if (k >= priorities_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)priorities_arraysize, (unsigned long)k);
    this->priorities[k] = priorities;
}

void TDMAGrantRequest::insertPriorities(size_t k, double priorities)
{
    if (k > priorities_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)priorities_arraysize, (unsigned long)k);
    size_t newSize = priorities_arraysize + 1;
    double *priorities2 = new double[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        priorities2[i] = this->priorities[i];
    priorities2[k] = priorities;
    for (i = k + 1; i < newSize; i++)
        priorities2[i] = this->priorities[i-1];
    delete [] this->priorities;
    this->priorities = priorities2;
    priorities_arraysize = newSize;
}

void TDMAGrantRequest::appendPriorities(double priorities)
{
    insertPriorities(priorities_arraysize, priorities);
}

void TDMAGrantRequest::erasePriorities(size_t k)
{
    if (k >= priorities_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)priorities_arraysize, (unsigned long)k);
    size_t newSize = priorities_arraysize - 1;
    double *priorities2 = (newSize == 0) ? nullptr : new double[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        priorities2[i] = this->priorities[i];
    for (i = k; i < newSize; i++)
        priorities2[i] = this->priorities[i+1];
    delete [] this->priorities;
    this->priorities = priorities2;
    priorities_arraysize = newSize;
}

size_t TDMAGrantRequest::getOccupancyInfoArraySize() const
{
    return occupancyInfo_arraysize;
}

int TDMAGrantRequest::getOccupancyInfo(size_t k) const
{
    if (k >= occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    return this->occupancyInfo[k];
}

void TDMAGrantRequest::setOccupancyInfoArraySize(size_t newSize)
{
    int *occupancyInfo2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = occupancyInfo_arraysize < newSize ? occupancyInfo_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        occupancyInfo2[i] = this->occupancyInfo[i];
    for (size_t i = minSize; i < newSize; i++)
        occupancyInfo2[i] = 0;
    delete [] this->occupancyInfo;
    this->occupancyInfo = occupancyInfo2;
    occupancyInfo_arraysize = newSize;
}

void TDMAGrantRequest::setOccupancyInfo(size_t k, int occupancyInfo)
{
    if (k >= occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    this->occupancyInfo[k] = occupancyInfo;
}

void TDMAGrantRequest::insertOccupancyInfo(size_t k, int occupancyInfo)
{
    if (k > occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    size_t newSize = occupancyInfo_arraysize + 1;
    int *occupancyInfo2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyInfo2[i] = this->occupancyInfo[i];
    occupancyInfo2[k] = occupancyInfo;
    for (i = k + 1; i < newSize; i++)
        occupancyInfo2[i] = this->occupancyInfo[i-1];
    delete [] this->occupancyInfo;
    this->occupancyInfo = occupancyInfo2;
    occupancyInfo_arraysize = newSize;
}

void TDMAGrantRequest::appendOccupancyInfo(int occupancyInfo)
{
    insertOccupancyInfo(occupancyInfo_arraysize, occupancyInfo);
}

void TDMAGrantRequest::eraseOccupancyInfo(size_t k)
{
    if (k >= occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    size_t newSize = occupancyInfo_arraysize - 1;
    int *occupancyInfo2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyInfo2[i] = this->occupancyInfo[i];
    for (i = k; i < newSize; i++)
        occupancyInfo2[i] = this->occupancyInfo[i+1];
    delete [] this->occupancyInfo;
    this->occupancyInfo = occupancyInfo2;
    occupancyInfo_arraysize = newSize;
}

size_t TDMAGrantRequest::getOccupancyHopsArraySize() const
{
    return occupancyHops_arraysize;
}

int TDMAGrantRequest::getOccupancyHops(size_t k) const
{
    if (k >= occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    return this->occupancyHops[k];
}

void TDMAGrantRequest::setOccupancyHopsArraySize(size_t newSize)
{
    int *occupancyHops2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = occupancyHops_arraysize < newSize ? occupancyHops_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        occupancyHops2[i] = this->occupancyHops[i];
    for (size_t i = minSize; i < newSize; i++)
        occupancyHops2[i] = 0;
    delete [] this->occupancyHops;
    this->occupancyHops = occupancyHops2;
    occupancyHops_arraysize = newSize;
}

void TDMAGrantRequest::setOccupancyHops(size_t k, int occupancyHops)
{
    if (k >= occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    this->occupancyHops[k] = occupancyHops;
}

void TDMAGrantRequest::insertOccupancyHops(size_t k, int occupancyHops)
{
    if (k > occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    size_t newSize = occupancyHops_arraysize + 1;
    int *occupancyHops2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyHops2[i] = this->occupancyHops[i];
    occupancyHops2[k] = occupancyHops;
    for (i = k + 1; i < newSize; i++)
        occupancyHops2[i] = this->occupancyHops[i-1];
    delete [] this->occupancyHops;
    this->occupancyHops = occupancyHops2;
    occupancyHops_arraysize = newSize;
}

void TDMAGrantRequest::appendOccupancyHops(int occupancyHops)
{
    insertOccupancyHops(occupancyHops_arraysize, occupancyHops);
}

void TDMAGrantRequest::eraseOccupancyHops(size_t k)
{
    if (k >= occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    size_t newSize = occupancyHops_arraysize - 1;
    int *occupancyHops2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyHops2[i] = this->occupancyHops[i];
    for (i = k; i < newSize; i++)
        occupancyHops2[i] = this->occupancyHops[i+1];
    delete [] this->occupancyHops;
    this->occupancyHops = occupancyHops2;
    occupancyHops_arraysize = newSize;
}

class TDMAGrantRequestDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_srcId,
        FIELD_targetNodeIds,
        FIELD_priorities,
        FIELD_occupancyInfo,
        FIELD_occupancyHops,
    };
  public:
    TDMAGrantRequestDescriptor();
    virtual ~TDMAGrantRequestDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TDMAGrantRequestDescriptor)

TDMAGrantRequestDescriptor::TDMAGrantRequestDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(TDMAGrantRequest)), "omnetpp::cPacket")
{
    propertyNames = nullptr;
}

TDMAGrantRequestDescriptor::~TDMAGrantRequestDescriptor()
{
    delete[] propertyNames;
}

bool TDMAGrantRequestDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TDMAGrantRequest *>(obj)!=nullptr;
}

const char **TDMAGrantRequestDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TDMAGrantRequestDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TDMAGrantRequestDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 5+base->getFieldCount() : 5;
}

unsigned int TDMAGrantRequestDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_srcId
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_targetNodeIds
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_priorities
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_occupancyInfo
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_occupancyHops
    };
    return (field >= 0 && field < 5) ? fieldTypeFlags[field] : 0;
}

const char *TDMAGrantRequestDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "srcId",
        "targetNodeIds",
        "priorities",
        "occupancyInfo",
        "occupancyHops",
    };
    return (field >= 0 && field < 5) ? fieldNames[field] : nullptr;
}

int TDMAGrantRequestDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "srcId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "targetNodeIds") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "priorities") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "occupancyInfo") == 0) return baseIndex + 3;
    if (strcmp(fieldName, "occupancyHops") == 0) return baseIndex + 4;
    return base ? base->findField(fieldName) : -1;
}

const char *TDMAGrantRequestDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_srcId
        "int",    // FIELD_targetNodeIds
        "double",    // FIELD_priorities
        "int",    // FIELD_occupancyInfo
        "int",    // FIELD_occupancyHops
    };
    return (field >= 0 && field < 5) ? fieldTypeStrings[field] : nullptr;
}

const char **TDMAGrantRequestDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TDMAGrantRequestDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TDMAGrantRequestDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        case FIELD_targetNodeIds: return pp->getTargetNodeIdsArraySize();
        case FIELD_priorities: return pp->getPrioritiesArraySize();
        case FIELD_occupancyInfo: return pp->getOccupancyInfoArraySize();
        case FIELD_occupancyHops: return pp->getOccupancyHopsArraySize();
        default: return 0;
    }
}

void TDMAGrantRequestDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        case FIELD_targetNodeIds: pp->setTargetNodeIdsArraySize(size); break;
        case FIELD_priorities: pp->setPrioritiesArraySize(size); break;
        case FIELD_occupancyInfo: pp->setOccupancyInfoArraySize(size); break;
        case FIELD_occupancyHops: pp->setOccupancyHopsArraySize(size); break;
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TDMAGrantRequest'", field);
    }
}

const char *TDMAGrantRequestDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TDMAGrantRequestDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: return long2string(pp->getSrcId());
        case FIELD_targetNodeIds: return long2string(pp->getTargetNodeIds(i));
        case FIELD_priorities: return double2string(pp->getPriorities(i));
        case FIELD_occupancyInfo: return long2string(pp->getOccupancyInfo(i));
        case FIELD_occupancyHops: return long2string(pp->getOccupancyHops(i));
        default: return "";
    }
}

void TDMAGrantRequestDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: pp->setSrcId(string2long(value)); break;
        case FIELD_targetNodeIds: pp->setTargetNodeIds(i,string2long(value)); break;
        case FIELD_priorities: pp->setPriorities(i,string2double(value)); break;
        case FIELD_occupancyInfo: pp->setOccupancyInfo(i,string2long(value)); break;
        case FIELD_occupancyHops: pp->setOccupancyHops(i,string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMAGrantRequest'", field);
    }
}

omnetpp::cValue TDMAGrantRequestDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: return pp->getSrcId();
        case FIELD_targetNodeIds: return pp->getTargetNodeIds(i);
        case FIELD_priorities: return pp->getPriorities(i);
        case FIELD_occupancyInfo: return pp->getOccupancyInfo(i);
        case FIELD_occupancyHops: return pp->getOccupancyHops(i);
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TDMAGrantRequest' as cValue -- field index out of range?", field);
    }
}

void TDMAGrantRequestDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: pp->setSrcId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_targetNodeIds: pp->setTargetNodeIds(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_priorities: pp->setPriorities(i,value.doubleValue()); break;
        case FIELD_occupancyInfo: pp->setOccupancyInfo(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_occupancyHops: pp->setOccupancyHops(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMAGrantRequest'", field);
    }
}

const char *TDMAGrantRequestDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TDMAGrantRequestDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TDMAGrantRequestDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantRequest *pp = omnetpp::fromAnyPtr<TDMAGrantRequest>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMAGrantRequest'", field);
    }
}

Register_Class(TDMAGrantReply)

TDMAGrantReply::TDMAGrantReply(const char *name, short kind) : ::omnetpp::cPacket(name, kind)
{
}

TDMAGrantReply::TDMAGrantReply(const TDMAGrantReply& other) : ::omnetpp::cPacket(other)
{
    copy(other);
}

TDMAGrantReply::~TDMAGrantReply()
{
    delete [] this->slotGrantDecisions;
    delete [] this->occupancyInfo;
    delete [] this->occupancyHops;
}

TDMAGrantReply& TDMAGrantReply::operator=(const TDMAGrantReply& other)
{
    if (this == &other) return *this;
    ::omnetpp::cPacket::operator=(other);
    copy(other);
    return *this;
}

void TDMAGrantReply::copy(const TDMAGrantReply& other)
{
    this->srcId = other.srcId;
    delete [] this->slotGrantDecisions;
    this->slotGrantDecisions = (other.slotGrantDecisions_arraysize==0) ? nullptr : new int[other.slotGrantDecisions_arraysize];
    slotGrantDecisions_arraysize = other.slotGrantDecisions_arraysize;
    for (size_t i = 0; i < slotGrantDecisions_arraysize; i++) {
        this->slotGrantDecisions[i] = other.slotGrantDecisions[i];
    }
    delete [] this->occupancyInfo;
    this->occupancyInfo = (other.occupancyInfo_arraysize==0) ? nullptr : new int[other.occupancyInfo_arraysize];
    occupancyInfo_arraysize = other.occupancyInfo_arraysize;
    for (size_t i = 0; i < occupancyInfo_arraysize; i++) {
        this->occupancyInfo[i] = other.occupancyInfo[i];
    }
    delete [] this->occupancyHops;
    this->occupancyHops = (other.occupancyHops_arraysize==0) ? nullptr : new int[other.occupancyHops_arraysize];
    occupancyHops_arraysize = other.occupancyHops_arraysize;
    for (size_t i = 0; i < occupancyHops_arraysize; i++) {
        this->occupancyHops[i] = other.occupancyHops[i];
    }
}

void TDMAGrantReply::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::omnetpp::cPacket::parsimPack(b);
    doParsimPacking(b,this->srcId);
    b->pack(slotGrantDecisions_arraysize);
    doParsimArrayPacking(b,this->slotGrantDecisions,slotGrantDecisions_arraysize);
    b->pack(occupancyInfo_arraysize);
    doParsimArrayPacking(b,this->occupancyInfo,occupancyInfo_arraysize);
    b->pack(occupancyHops_arraysize);
    doParsimArrayPacking(b,this->occupancyHops,occupancyHops_arraysize);
}

void TDMAGrantReply::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::omnetpp::cPacket::parsimUnpack(b);
    doParsimUnpacking(b,this->srcId);
    delete [] this->slotGrantDecisions;
    b->unpack(slotGrantDecisions_arraysize);
    if (slotGrantDecisions_arraysize == 0) {
        this->slotGrantDecisions = nullptr;
    } else {
        this->slotGrantDecisions = new int[slotGrantDecisions_arraysize];
        doParsimArrayUnpacking(b,this->slotGrantDecisions,slotGrantDecisions_arraysize);
    }
    delete [] this->occupancyInfo;
    b->unpack(occupancyInfo_arraysize);
    if (occupancyInfo_arraysize == 0) {
        this->occupancyInfo = nullptr;
    } else {
        this->occupancyInfo = new int[occupancyInfo_arraysize];
        doParsimArrayUnpacking(b,this->occupancyInfo,occupancyInfo_arraysize);
    }
    delete [] this->occupancyHops;
    b->unpack(occupancyHops_arraysize);
    if (occupancyHops_arraysize == 0) {
        this->occupancyHops = nullptr;
    } else {
        this->occupancyHops = new int[occupancyHops_arraysize];
        doParsimArrayUnpacking(b,this->occupancyHops,occupancyHops_arraysize);
    }
}

int TDMAGrantReply::getSrcId() const
{
    return this->srcId;
}

void TDMAGrantReply::setSrcId(int srcId)
{
    this->srcId = srcId;
}

size_t TDMAGrantReply::getSlotGrantDecisionsArraySize() const
{
    return slotGrantDecisions_arraysize;
}

int TDMAGrantReply::getSlotGrantDecisions(size_t k) const
{
    if (k >= slotGrantDecisions_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)slotGrantDecisions_arraysize, (unsigned long)k);
    return this->slotGrantDecisions[k];
}

void TDMAGrantReply::setSlotGrantDecisionsArraySize(size_t newSize)
{
    int *slotGrantDecisions2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = slotGrantDecisions_arraysize < newSize ? slotGrantDecisions_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        slotGrantDecisions2[i] = this->slotGrantDecisions[i];
    for (size_t i = minSize; i < newSize; i++)
        slotGrantDecisions2[i] = 0;
    delete [] this->slotGrantDecisions;
    this->slotGrantDecisions = slotGrantDecisions2;
    slotGrantDecisions_arraysize = newSize;
}

void TDMAGrantReply::setSlotGrantDecisions(size_t k, int slotGrantDecisions)
{
    if (k >= slotGrantDecisions_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)slotGrantDecisions_arraysize, (unsigned long)k);
    this->slotGrantDecisions[k] = slotGrantDecisions;
}

void TDMAGrantReply::insertSlotGrantDecisions(size_t k, int slotGrantDecisions)
{
    if (k > slotGrantDecisions_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)slotGrantDecisions_arraysize, (unsigned long)k);
    size_t newSize = slotGrantDecisions_arraysize + 1;
    int *slotGrantDecisions2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        slotGrantDecisions2[i] = this->slotGrantDecisions[i];
    slotGrantDecisions2[k] = slotGrantDecisions;
    for (i = k + 1; i < newSize; i++)
        slotGrantDecisions2[i] = this->slotGrantDecisions[i-1];
    delete [] this->slotGrantDecisions;
    this->slotGrantDecisions = slotGrantDecisions2;
    slotGrantDecisions_arraysize = newSize;
}

void TDMAGrantReply::appendSlotGrantDecisions(int slotGrantDecisions)
{
    insertSlotGrantDecisions(slotGrantDecisions_arraysize, slotGrantDecisions);
}

void TDMAGrantReply::eraseSlotGrantDecisions(size_t k)
{
    if (k >= slotGrantDecisions_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)slotGrantDecisions_arraysize, (unsigned long)k);
    size_t newSize = slotGrantDecisions_arraysize - 1;
    int *slotGrantDecisions2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        slotGrantDecisions2[i] = this->slotGrantDecisions[i];
    for (i = k; i < newSize; i++)
        slotGrantDecisions2[i] = this->slotGrantDecisions[i+1];
    delete [] this->slotGrantDecisions;
    this->slotGrantDecisions = slotGrantDecisions2;
    slotGrantDecisions_arraysize = newSize;
}

size_t TDMAGrantReply::getOccupancyInfoArraySize() const
{
    return occupancyInfo_arraysize;
}

int TDMAGrantReply::getOccupancyInfo(size_t k) const
{
    if (k >= occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    return this->occupancyInfo[k];
}

void TDMAGrantReply::setOccupancyInfoArraySize(size_t newSize)
{
    int *occupancyInfo2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = occupancyInfo_arraysize < newSize ? occupancyInfo_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        occupancyInfo2[i] = this->occupancyInfo[i];
    for (size_t i = minSize; i < newSize; i++)
        occupancyInfo2[i] = 0;
    delete [] this->occupancyInfo;
    this->occupancyInfo = occupancyInfo2;
    occupancyInfo_arraysize = newSize;
}

void TDMAGrantReply::setOccupancyInfo(size_t k, int occupancyInfo)
{
    if (k >= occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    this->occupancyInfo[k] = occupancyInfo;
}

void TDMAGrantReply::insertOccupancyInfo(size_t k, int occupancyInfo)
{
    if (k > occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    size_t newSize = occupancyInfo_arraysize + 1;
    int *occupancyInfo2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyInfo2[i] = this->occupancyInfo[i];
    occupancyInfo2[k] = occupancyInfo;
    for (i = k + 1; i < newSize; i++)
        occupancyInfo2[i] = this->occupancyInfo[i-1];
    delete [] this->occupancyInfo;
    this->occupancyInfo = occupancyInfo2;
    occupancyInfo_arraysize = newSize;
}

void TDMAGrantReply::appendOccupancyInfo(int occupancyInfo)
{
    insertOccupancyInfo(occupancyInfo_arraysize, occupancyInfo);
}

void TDMAGrantReply::eraseOccupancyInfo(size_t k)
{
    if (k >= occupancyInfo_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyInfo_arraysize, (unsigned long)k);
    size_t newSize = occupancyInfo_arraysize - 1;
    int *occupancyInfo2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyInfo2[i] = this->occupancyInfo[i];
    for (i = k; i < newSize; i++)
        occupancyInfo2[i] = this->occupancyInfo[i+1];
    delete [] this->occupancyInfo;
    this->occupancyInfo = occupancyInfo2;
    occupancyInfo_arraysize = newSize;
}

size_t TDMAGrantReply::getOccupancyHopsArraySize() const
{
    return occupancyHops_arraysize;
}

int TDMAGrantReply::getOccupancyHops(size_t k) const
{
    if (k >= occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    return this->occupancyHops[k];
}

void TDMAGrantReply::setOccupancyHopsArraySize(size_t newSize)
{
    int *occupancyHops2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = occupancyHops_arraysize < newSize ? occupancyHops_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        occupancyHops2[i] = this->occupancyHops[i];
    for (size_t i = minSize; i < newSize; i++)
        occupancyHops2[i] = 0;
    delete [] this->occupancyHops;
    this->occupancyHops = occupancyHops2;
    occupancyHops_arraysize = newSize;
}

void TDMAGrantReply::setOccupancyHops(size_t k, int occupancyHops)
{
    if (k >= occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    this->occupancyHops[k] = occupancyHops;
}

void TDMAGrantReply::insertOccupancyHops(size_t k, int occupancyHops)
{
    if (k > occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    size_t newSize = occupancyHops_arraysize + 1;
    int *occupancyHops2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyHops2[i] = this->occupancyHops[i];
    occupancyHops2[k] = occupancyHops;
    for (i = k + 1; i < newSize; i++)
        occupancyHops2[i] = this->occupancyHops[i-1];
    delete [] this->occupancyHops;
    this->occupancyHops = occupancyHops2;
    occupancyHops_arraysize = newSize;
}

void TDMAGrantReply::appendOccupancyHops(int occupancyHops)
{
    insertOccupancyHops(occupancyHops_arraysize, occupancyHops);
}

void TDMAGrantReply::eraseOccupancyHops(size_t k)
{
    if (k >= occupancyHops_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)occupancyHops_arraysize, (unsigned long)k);
    size_t newSize = occupancyHops_arraysize - 1;
    int *occupancyHops2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        occupancyHops2[i] = this->occupancyHops[i];
    for (i = k; i < newSize; i++)
        occupancyHops2[i] = this->occupancyHops[i+1];
    delete [] this->occupancyHops;
    this->occupancyHops = occupancyHops2;
    occupancyHops_arraysize = newSize;
}

class TDMAGrantReplyDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_srcId,
        FIELD_slotGrantDecisions,
        FIELD_occupancyInfo,
        FIELD_occupancyHops,
    };
  public:
    TDMAGrantReplyDescriptor();
    virtual ~TDMAGrantReplyDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TDMAGrantReplyDescriptor)

TDMAGrantReplyDescriptor::TDMAGrantReplyDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(TDMAGrantReply)), "omnetpp::cPacket")
{
    propertyNames = nullptr;
}

TDMAGrantReplyDescriptor::~TDMAGrantReplyDescriptor()
{
    delete[] propertyNames;
}

bool TDMAGrantReplyDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TDMAGrantReply *>(obj)!=nullptr;
}

const char **TDMAGrantReplyDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TDMAGrantReplyDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TDMAGrantReplyDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 4+base->getFieldCount() : 4;
}

unsigned int TDMAGrantReplyDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_srcId
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_slotGrantDecisions
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_occupancyInfo
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_occupancyHops
    };
    return (field >= 0 && field < 4) ? fieldTypeFlags[field] : 0;
}

const char *TDMAGrantReplyDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "srcId",
        "slotGrantDecisions",
        "occupancyInfo",
        "occupancyHops",
    };
    return (field >= 0 && field < 4) ? fieldNames[field] : nullptr;
}

int TDMAGrantReplyDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "srcId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "slotGrantDecisions") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "occupancyInfo") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "occupancyHops") == 0) return baseIndex + 3;
    return base ? base->findField(fieldName) : -1;
}

const char *TDMAGrantReplyDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_srcId
        "int",    // FIELD_slotGrantDecisions
        "int",    // FIELD_occupancyInfo
        "int",    // FIELD_occupancyHops
    };
    return (field >= 0 && field < 4) ? fieldTypeStrings[field] : nullptr;
}

const char **TDMAGrantReplyDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TDMAGrantReplyDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TDMAGrantReplyDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        case FIELD_slotGrantDecisions: return pp->getSlotGrantDecisionsArraySize();
        case FIELD_occupancyInfo: return pp->getOccupancyInfoArraySize();
        case FIELD_occupancyHops: return pp->getOccupancyHopsArraySize();
        default: return 0;
    }
}

void TDMAGrantReplyDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        case FIELD_slotGrantDecisions: pp->setSlotGrantDecisionsArraySize(size); break;
        case FIELD_occupancyInfo: pp->setOccupancyInfoArraySize(size); break;
        case FIELD_occupancyHops: pp->setOccupancyHopsArraySize(size); break;
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TDMAGrantReply'", field);
    }
}

const char *TDMAGrantReplyDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TDMAGrantReplyDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: return long2string(pp->getSrcId());
        case FIELD_slotGrantDecisions: return long2string(pp->getSlotGrantDecisions(i));
        case FIELD_occupancyInfo: return long2string(pp->getOccupancyInfo(i));
        case FIELD_occupancyHops: return long2string(pp->getOccupancyHops(i));
        default: return "";
    }
}

void TDMAGrantReplyDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: pp->setSrcId(string2long(value)); break;
        case FIELD_slotGrantDecisions: pp->setSlotGrantDecisions(i,string2long(value)); break;
        case FIELD_occupancyInfo: pp->setOccupancyInfo(i,string2long(value)); break;
        case FIELD_occupancyHops: pp->setOccupancyHops(i,string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMAGrantReply'", field);
    }
}

omnetpp::cValue TDMAGrantReplyDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: return pp->getSrcId();
        case FIELD_slotGrantDecisions: return pp->getSlotGrantDecisions(i);
        case FIELD_occupancyInfo: return pp->getOccupancyInfo(i);
        case FIELD_occupancyHops: return pp->getOccupancyHops(i);
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TDMAGrantReply' as cValue -- field index out of range?", field);
    }
}

void TDMAGrantReplyDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        case FIELD_srcId: pp->setSrcId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_slotGrantDecisions: pp->setSlotGrantDecisions(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_occupancyInfo: pp->setOccupancyInfo(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_occupancyHops: pp->setOccupancyHops(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMAGrantReply'", field);
    }
}

const char *TDMAGrantReplyDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TDMAGrantReplyDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TDMAGrantReplyDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TDMAGrantReply *pp = omnetpp::fromAnyPtr<TDMAGrantReply>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TDMAGrantReply'", field);
    }
}

namespace omnetpp {

}  // namespace omnetpp

