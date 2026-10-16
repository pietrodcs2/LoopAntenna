/*
 * LA_DATA_TUNING - FIXED-LENGTH TUNING DATABASE SUPPORT
 * ===================================================
 * This module defines data structures, layout, offsets, CRC and serialization.
 * It does not read/program/erase FLASH and does not allocate complete databases
 * in RAM. It has no filesystem or NXP SDK dependency. All pointers/buffers are
 * caller-owned. Public configuration macros and types are in LA_DATA_TUNING.h;
 * documentation intentionally resides in this source file.
 *
 * FREQUENCY GRID AND LOADS
 * DATA_FREQUENCY_INTERVAL_HZ controls the grid (default 10 kHz). Band limits
 * default to 2..30 MHz and must align to the step. Both endpoints have records.
 * Intermediate frequencies use the lower reference: 14,000,000..14,009,999 Hz
 * map to 14,000,000 Hz. The final record represents exactly the upper endpoint.
 * Out-of-band frequencies are rejected. Changing grid/version requires explicit
 * migration/recreation of existing databases; an incompatible header is refused.
 *
 * A loadId identifies an external load, not a frequency or FLASH address.
 * InitCatalog creates only a small descriptor table for 1..maximum loadIds.
 * Each descriptor identifies its format and logical base address; the default
 * maximum is five instances. An instance may exist while another is absent.
 * No function automatically detects the connected external load.
 *
 * FORMAT AND RAM USE
 * A binary tuning record is 36 bytes. A fixed XML record is 124 stored bytes.
 * XML buffers need one additional byte for the C terminator, which is not stored.
 * XML header = 149 bytes; footer = 12 bytes. ASCII/UTF-8 without BOM, single LF,
 * exact attribute order/spacing, ten decimal digits and eight uppercase CRC hex
 * digits. This is a strict XML storage representation, not a general XML parser.
 * Struct fields hold physical capacitance in pF and inductance in nH, not masks.
 * useCount=0 means unused with all six L/C values zero. On first accepted optimum
 * set current=min=max and count=1. Later accepted optima expand min/max; count
 * successful uses including reuse, saturating at UINT32_MAX. The application
 * owns that statistics policy; this module does not increment counters.
 *
 * ENUMS AND GENERIC CONVERSION
 * Header/record enum indices fix the mapping between members and numeric arrays.
 * Semantic conversion functions use named indices. Generic text/CRC functions
 * process array elements in ascending order without knowing physical meanings.
 * The CRC is the last field, excluded from its own calculation. Changing field
 * order, format or widths changes the stored protocol and requires a version bump.
 * CRC-32/ISO-HDLC uses explicit little-endian serialization, never native struct
 * bytes. It detects accidental corruption; it is not authentication or recovery.
 *
 * STARTUP: DETERMINE WHETHER EACH DATABASE EXISTS
 * 1. Configure storage and initialize the catalog with unique loadIds/base.
 * 2. Read only the fixed header bytes at each descriptor's base. A read error is
 *    an I/O error, not evidence of an empty database.
 * 3. Pass bytes to LA_DATA_InspectDatabaseHeader(). It checks XML/binary syntax,
 *    header CRC, version, frequency grid, record size and matching loadId.
 *    Missing means every header byte is DATA_FLASH_ERASED_BYTE (default 0xFF).
 *    HeaderValid means matching metadata, not a scan of all stored records.
 *    InvalidHeader/Incompatible must not automatically trigger destructive erase.
 * 4. For Missing, create an XML header, one unused record at a time and footer.
 *    The storage example writes the valid header LAST after payload verification
 *    and sync. This keeps RAM bounded and allows independent instance creation.
 *
 * LA_DATA_IsHeaderValid() is a boolean check of a decoded header; it does not
 * read FLASH and has no expected loadId. InspectDatabaseHeader adds the selected
 * descriptor identity check. DecodeHeaderXML also validates before publishing.
 * LA_DATA_IsRecordValid() is likewise a boolean check of a decoded tuning record.
 * Detailed error statuses remain available through decode and inspect operations.
 *
 * ON-DEMAND RECORD ACCESS
 * GetRecordLocation gives index, frequency interval, length, relative and absolute
 * offsets. Use the relative offset for seek in an individual file; use absolute
 * offset only when descriptors represent a shared FLASH/container address space.
 * Read exactly the reported length, decode/validate the record and confirm its
 * frequency_Hz equals the slot reference. Header validity does not guarantee
 * every record. useCount=0 means the slot has no accepted optimum yet.
 * After editing a record call UpdateRecordCrc(), EncodeRecordXML(), then store
 * exactly DATA_RECORD_XML_SIZE_BYTES. A console type command can stream one
 * instance or a range of record lines without loading its complete contents.
 *
 * STORAGE RESPONSIBILITIES
 * Offsets are logical. The future driver/filesystem must handle actual capacity,
 * sector erasure/preservation, ordering, wear and interrupted write recovery.
 * Alignment macros reserve space only; they are not a durability implementation.
 * A fixed record length does not make physical FLASH overwrite atomic or safe.
 * See example_usage.c for independent probe/create steps and backend hook rules.
 *
 * OUTPUT RULES
 * Public decode/layout/location functions publish outputs only on success unless
 * specifically documented otherwise. Inspection always initializes its state
 * output. Never use optional decoded outputs when validity has not been established.
 */
#include "LA_DATA_TUNING.h"
#include <string.h>


#define DATA_RECORD_FIELD_COUNT ((unsigned)eDATA_RecordField_Count)
#define DATA_RECORD_CRC_FIELD_COUNT ((unsigned)eDATA_RecordField_Crc)
#define DATA_HEADER_FIELD_COUNT ((unsigned)eDATA_HeaderField_Count)
#define DATA_HEADER_CRC_FIELD_COUNT ((unsigned)eDATA_HeaderField_Crc)
#define DATA_U32_BYTES 4U
#define DATA_CRC32_INIT UINT32_C(0xFFFFFFFF)
#define DATA_CRC32_POLY UINT32_C(0xEDB88320)


static const char DATA_recordTemplate[] = DATA_XML_RECORD_TEMPLATE;
static const char DATA_headerTemplate[] = DATA_XML_HEADER_TEMPLATE;
static const char DATA_hexDigits[] = "0123456789ABCDEF";


/*
 * Internal format selector check.
 * INPUT: format: candidate enumeration.
 * OUTPUT/RETURN: Boolean true only for Binary or XML; no outputs or storage access.
 */
static bool LA_DATA_IsFormatValid(LA_DATA_Format_t format)
{
    return format == eLA_DATA_Format_Binary || format == eLA_DATA_Format_XML;
}

/*
 * Returns binary record size, including its CRC.
 * INPUT: None.
 * OUTPUT/RETURN: Return: byte count of one record (36 in version 1).
 */
uint32_t LA_DATA_GetRecordBinarySize(void) { return DATA_RECORD_BINARY_SIZE_BYTES; }
/*
 * Returns the complete optional binary database layout size.
 * INPUT: None.
 * OUTPUT/RETURN: Return: binary header plus all records; no footer, allocator or filesystem overhead.
 */
uint64_t LA_DATA_GetDatabaseBinarySize(void) { return DATA_DATABASE_BINARY_SIZE_BYTES; }
/*
 * Returns exact stored byte length of one complete XML database.
 * INPUT: None.
 * OUTPUT/RETURN: Return: fixed header + all fixed records + footer, excluding C NULs and padding.
 */
uint64_t LA_DATA_GetDatabaseXMLSize(void) { return DATA_DATABASE_XML_SIZE_BYTES; }

/*
 * Calculates payload, header/footer and reservation sizes.
 * INPUT: format: Binary/XML; layout: non-NULL destination.
 * OUTPUT/RETURN: On Success, fills layout. databaseSize excludes alignment padding; allocationSize includes it.
 * Invalid format/pointer or arithmetic overflow leaves the destination unchanged.
 */
LA_DATA_Status_t LA_DATA_GetLayout(LA_DATA_Format_t format, LA_DATA_Layout_t *layout)
{
    LA_DATA_Layout_t value;
    uint64_t padding;
    if (layout == NULL || !LA_DATA_IsFormatValid(format)) return eLA_DATA_InvalidArgument;
    value.recordCount = (uint32_t)DATA_RECORD_COUNT;
    value.binaryRecordSize_bytes = DATA_RECORD_BINARY_SIZE_BYTES;
    value.binaryPayloadSize_bytes = DATA_DATABASE_BINARY_PAYLOAD_BYTES;
    value.storedRecordSize_bytes = format == eLA_DATA_Format_XML ?
        DATA_RECORD_XML_SIZE_BYTES : DATA_RECORD_BINARY_SIZE_BYTES;
    value.headerSize_bytes = format == eLA_DATA_Format_XML ?
        DATA_HEADER_XML_SIZE_BYTES : DATA_HEADER_BINARY_SIZE_BYTES;
    value.footerSize_bytes = format == eLA_DATA_Format_XML ? DATA_FOOTER_XML_SIZE_BYTES : 0U;
    value.storedPayloadSize_bytes = DATA_RECORD_COUNT * value.storedRecordSize_bytes;
    value.databaseSize_bytes = value.headerSize_bytes + value.storedPayloadSize_bytes + value.footerSize_bytes;
    padding = (DATA_DATABASE_ALIGNMENT_BYTES -
        value.databaseSize_bytes % DATA_DATABASE_ALIGNMENT_BYTES) % DATA_DATABASE_ALIGNMENT_BYTES;
    if (value.databaseSize_bytes > UINT64_MAX - padding) return eLA_DATA_Overflow;
    value.allocationSize_bytes = value.databaseSize_bytes + padding;
    *layout = value;
    return eLA_DATA_Success;
}

/*
 * Maps a frequency to the lower reference slot.
 * INPUT: frequency_Hz: requested frequency; index: non-NULL destination.
 * OUTPUT/RETURN: On Success, writes a zero-based index. Both limits are included.
 * Out-of-band input is rejected, never clamped; output remains unchanged on failure.
 */
LA_DATA_Status_t LA_DATA_FrequencyToIndex(uint32_t frequency_Hz, uint32_t *index)
{
    if (index == NULL) return eLA_DATA_InvalidArgument;
    if (frequency_Hz < DATA_FREQUENCY_MIN_HZ || frequency_Hz > DATA_FREQUENCY_MAX_HZ)
        return eLA_DATA_FrequencyOutOfRange;
    *index = (uint32_t)((frequency_Hz - DATA_FREQUENCY_MIN_HZ) / DATA_FREQUENCY_INTERVAL_HZ);
    return eLA_DATA_Success;
}

/*
 * Calculates the reference frequency of a slot.
 * INPUT: index: zero-based slot; frequency_Hz: non-NULL destination.
 * OUTPUT/RETURN: On Success, writes fmin + index*step. Invalid index/pointer does not alter output.
 */
LA_DATA_Status_t LA_DATA_IndexToFrequency( uint32_t index, uint32_t *frequency_Hz )
{
    if (frequency_Hz == NULL) return eLA_DATA_InvalidArgument;
    if (index >= DATA_RECORD_COUNT) return eLA_DATA_IndexOutOfRange;
    *frequency_Hz = (uint32_t)(DATA_FREQUENCY_MIN_HZ + (uint64_t)index * DATA_FREQUENCY_INTERVAL_HZ);
    return eLA_DATA_Success;
}

/*
 * Calculates a stored record offset relative to one database.
 * INPUT: format: representation; index: slot; relativeOffset_bytes: destination.
 * OUTPUT/RETURN: On Success, writes headerSize + index*recordStride. Does not include instance base
 * or alignment padding and does not read storage.
 */
LA_DATA_Status_t LA_DATA_GetRecordOffsetByIndex( LA_DATA_Format_t format,
    uint32_t index, uint64_t *relativeOffset_bytes )
{
    LA_DATA_Layout_t layout;
    LA_DATA_Status_t status;
    if (relativeOffset_bytes == NULL) return eLA_DATA_InvalidArgument;
    if (index >= DATA_RECORD_COUNT) return eLA_DATA_IndexOutOfRange;
    status = LA_DATA_GetLayout(format, &layout);
    if (status != eLA_DATA_Success) return status;
    *relativeOffset_bytes = layout.headerSize_bytes + (uint64_t)index * layout.storedRecordSize_bytes;
    return eLA_DATA_Success;
}

/*
 * Calculates a database-relative record offset from frequency.
 * INPUT: format, frequency_Hz: selection; relativeOffset_bytes: destination.
 * OUTPUT/RETURN: On Success, writes the lower-slot offset. Propagates range/index/layout errors.
 */
LA_DATA_Status_t LA_DATA_GetRecordOffsetByFrequency( LA_DATA_Format_t format,
    uint32_t frequency_Hz, uint64_t *relativeOffset_bytes )
{
    uint32_t index;
    LA_DATA_Status_t status = LA_DATA_FrequencyToIndex(frequency_Hz, &index);
    if (status != eLA_DATA_Success) return status;
    return LA_DATA_GetRecordOffsetByIndex(format, index, relativeOffset_bytes);
}

/*
 * Builds the complete address/interval description for a requested slot.
 * INPUT: database: descriptor with loadId/format/base; frequency_Hz: request; location: destination.
 * OUTPUT/RETURN: On Success, fills index, reference and inclusive interval, record length and both
 * offsets. Checks database-end overflow. Does not fetch LA_TUNING_Record_t data.
 */
LA_DATA_Status_t LA_DATA_GetRecordLocation(const LA_DATA_Database_t *database,
    uint32_t frequency_Hz, LA_DATA_RecordLocation_t *location)
{
    LA_DATA_RecordLocation_t value;
    LA_DATA_Layout_t layout;
    LA_DATA_Status_t status;
    if (database == NULL || location == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_GetLayout(database->format, &layout);
    if (status != eLA_DATA_Success) return status;
    status = LA_DATA_FrequencyToIndex(frequency_Hz, &value.index);
    if (status != eLA_DATA_Success) return status;
    (void)LA_DATA_IndexToFrequency(value.index, &value.referenceFrequency_Hz);
    value.intervalFirst_Hz = value.referenceFrequency_Hz;
    value.intervalLast_Hz = value.index == DATA_RECORD_COUNT - 1U ? DATA_FREQUENCY_MAX_HZ :
        (uint32_t)((uint64_t)value.referenceFrequency_Hz + DATA_FREQUENCY_INTERVAL_HZ - 1U);
    value.recordSize_bytes = layout.storedRecordSize_bytes;
    (void)LA_DATA_GetRecordOffsetByIndex(database->format, value.index, &value.relativeOffset_bytes);
    
    if (database->baseOffset_bytes > UINT64_MAX - layout.databaseSize_bytes) return eLA_DATA_Overflow;
    value.absoluteOffset_bytes = database->baseOffset_bytes + value.relativeOffset_bytes;
    *location = value;
    return eLA_DATA_Success;
}

/*
 * Initializes a bounded descriptor catalog for independent loads.
 * INPUT: catalog: destination; count: actual instance count; loadIds: unique identifiers;
 * format: common format; firstBase_bytes: aligned start of logical reservations.
 * OUTPUT/RETURN: On Success, fills descriptors at aligned strides. It performs no erase/write.
 * Rejects duplicates, invalid count/alignment and overflow without changing catalog.
 */
LA_DATA_Status_t LA_DATA_InitCatalog(LA_DATA_Catalog_t *catalog, uint32_t count,
    const uint32_t *loadIds, LA_DATA_Format_t format, uint64_t firstBase_bytes)
{
    LA_DATA_Layout_t layout;
    LA_DATA_Catalog_t value = {0};
    LA_DATA_Status_t status;
    if (catalog == NULL || loadIds == NULL || count == 0U || count > DATA_DATABASE_MAX_COUNT ||
        firstBase_bytes % DATA_DATABASE_ALIGNMENT_BYTES != 0U) return eLA_DATA_InvalidArgument;
    status = LA_DATA_GetLayout(format, &layout);
    if (status != eLA_DATA_Success) return status;
    if (layout.allocationSize_bytes > (UINT64_MAX - firstBase_bytes) / count) return eLA_DATA_Overflow;
    for (uint32_t i = 0U; i < count; ++i)
    {
        for (uint32_t j = 0U; j < i; ++j)
            if (loadIds[j] == loadIds[i]) return eLA_DATA_InvalidArgument;
        value.databases[i].loadId = loadIds[i];
        value.databases[i].format = format;
        value.databases[i].baseOffset_bytes = firstBase_bytes + (uint64_t)i * layout.allocationSize_bytes;
    }
    value.databaseCount = count;
    *catalog = value;
    return eLA_DATA_Success;
}

/*
 * Finds a descriptor by stable external-load identity.
 * INPUT: catalog: initialized table; loadId: requested identity.
 * OUTPUT/RETURN: Return: pointer into the caller catalog, or NULL if absent/invalid.
 * Pointer remains valid while the catalog itself stays alive and unchanged.
 */
const LA_DATA_Database_t *LA_DATA_FindDatabase(const LA_DATA_Catalog_t *catalog, uint32_t loadId)
{
    if (catalog == NULL || catalog->databaseCount > DATA_DATABASE_MAX_COUNT) return NULL;
    for (uint32_t i = 0U; i < catalog->databaseCount; ++i)
        if (catalog->databases[i].loadId == loadId) return &catalog->databases[i];
    return NULL;
}

/*
 * Sums aligned reservation sizes for the actual catalog entries.
 * INPUT: catalog: descriptors; size_bytes: non-NULL destination.
 * OUTPUT/RETURN: On Success, writes the total reservation bytes, not an absolute ending address.
 * It excludes backend-specific journals, sector copies and filesystem overhead.
 */
LA_DATA_Status_t LA_DATA_GetCatalogAllocatedSize(const LA_DATA_Catalog_t *catalog, uint64_t *size_bytes)
{
    uint64_t total = 0U;
    LA_DATA_Layout_t layout;
    if (catalog == NULL || size_bytes == NULL || catalog->databaseCount > DATA_DATABASE_MAX_COUNT)
        return eLA_DATA_InvalidArgument;
    for (uint32_t i = 0U; i < catalog->databaseCount; ++i)
    {
        LA_DATA_Status_t status = LA_DATA_GetLayout(catalog->databases[i].format, &layout);
        if (status != eLA_DATA_Success) return status;
        if (total > UINT64_MAX - layout.allocationSize_bytes) return eLA_DATA_Overflow;
        total += layout.allocationSize_bytes;
    }
    *size_bytes = total;
    return eLA_DATA_Success;
}

/*
 * Internal reflected CRC recurrence for one byte.
 * INPUT: crc: running unfinalized accumulator; byte: next input byte.
 * OUTPUT/RETURN: Return: next unfinalized accumulator. Performs no initial/final XOR.
 */
static uint32_t LA_DATA_UpdateCrcByte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) ? DATA_CRC32_POLY : 0U);
    return crc;
}

/*
 * Calculates CRC-32/ISO-HDLC over a supplied byte sequence.
 * INPUT: bytes: buffer (may be NULL only when count=0); count: number of bytes.
 * OUTPUT/RETURN: Return: finalized CRC. Empty input returns zero; invalid NULL/nonzero input also
 * returns zero, so callers must obey the pointer contract. No record interpretation.
 */
uint32_t LA_DATA_CalculateCrc32(const void *bytes, size_t count)
{
    const uint8_t *p = (const uint8_t *)bytes;
    uint32_t crc = DATA_CRC32_INIT;
    
    if (p == NULL && count != 0U) return 0U;
    for (size_t i = 0U; i < count; ++i) crc = LA_DATA_UpdateCrcByte(crc, p[i]);
    return crc ^ DATA_CRC32_INIT;
}

/*
 * Internal CRC over an ordered numeric array, independent of field meanings.
 * INPUT: fields: valid uint32_t array; count: number of elements to include.
 * OUTPUT/RETURN: Return: finalized CRC after four little-endian bytes per element.
 * The caller passes the CRC enum index as count to exclude the last CRC field.
 */
static uint32_t LA_DATA_CalculateFieldsCrc(const uint32_t *fields, unsigned count)
{
    uint32_t crc = DATA_CRC32_INIT;
    for (unsigned i = 0U; i < count; ++i)
        for (unsigned b = 0U; b < DATA_U32_BYTES; ++b)
            crc = LA_DATA_UpdateCrcByte(crc, (uint8_t)(fields[i] >> (8U * b)));
    return crc ^ DATA_CRC32_INIT;
}

/*
 * Internal semantic mapping from record members to enum-indexed fields.
 * INPUT: r: valid record; f: array of eDATA_RecordField_Count elements.
 * OUTPUT/RETURN: Copies every member including CRC; does not modify r or calculate CRC.
 */
static void LA_DATA_RecordToFields(const LA_TUNING_Record_t *r, uint32_t f[DATA_RECORD_FIELD_COUNT])
{
    f[eDATA_RecordField_Frequency] = r->frequency_Hz;
    f[eDATA_RecordField_Capacitance] = r->capacitance_pF;
    f[eDATA_RecordField_CapacitanceMin] = r->capacitanceMin_pF;
    f[eDATA_RecordField_CapacitanceMax] = r->capacitanceMax_pF;
    f[eDATA_RecordField_Inductance] = r->inductance_nH;
    f[eDATA_RecordField_InductanceMin] = r->inductanceMin_nH;
    f[eDATA_RecordField_InductanceMax] = r->inductanceMax_nH;
    f[eDATA_RecordField_UseCount] = r->useCount;
    f[eDATA_RecordField_Crc] = r->crc32;
}

/*
 * Internal inverse semantic mapping from enum-indexed fields.
 * INPUT: f: array of eDATA_RecordField_Count elements; r: destination record.
 * OUTPUT/RETURN: Copies every member, without validation. Caller validates before publishing.
 */
static void LA_DATA_FieldsToRecord(const uint32_t f[DATA_RECORD_FIELD_COUNT], LA_TUNING_Record_t *r)
{
    r->frequency_Hz = f[eDATA_RecordField_Frequency];
    r->capacitance_pF = f[eDATA_RecordField_Capacitance];
    r->capacitanceMin_pF = f[eDATA_RecordField_CapacitanceMin];
    r->capacitanceMax_pF = f[eDATA_RecordField_CapacitanceMax];
    r->inductance_nH = f[eDATA_RecordField_Inductance];
    r->inductanceMin_nH = f[eDATA_RecordField_InductanceMin];
    r->inductanceMax_nH = f[eDATA_RecordField_InductanceMax];
    r->useCount = f[eDATA_RecordField_UseCount];
    r->crc32 = f[eDATA_RecordField_Crc];
}

/*
 * Refreshes the CRC after an application changes a record.
 * INPUT: record: non-NULL mutable record.
 * OUTPUT/RETURN: On Success, replaces only record->crc32 using all preceding fields.
 * It does not repair frequencies/ranges/statistics or certify record consistency.
 */
LA_DATA_Status_t LA_DATA_UpdateRecordCrc(LA_TUNING_Record_t *record)
{
    uint32_t fields[DATA_RECORD_FIELD_COUNT];
    if (record == NULL) return eLA_DATA_InvalidArgument;
    LA_DATA_RecordToFields(record, fields);
    record->crc32 = LA_DATA_CalculateFieldsCrc(fields, DATA_RECORD_CRC_FIELD_COUNT);
    return eLA_DATA_Success;
}

/*
 * Internal detailed consistency check for a decoded tuning record.
 * INPUT: record: candidate, may be NULL.
 * OUTPUT/RETURN: Return: detailed status for pointer, reference-grid, CRC and L/C range checks.
 * Unused slots require zero L/C values; used slots require min<=current<=max.
 */
static LA_DATA_Status_t LA_DATA_CheckRecord(const LA_TUNING_Record_t *record)
{
    uint32_t fields[DATA_RECORD_FIELD_COUNT], index, reference;
    if (record == NULL) return eLA_DATA_InvalidArgument;
    if (LA_DATA_FrequencyToIndex(record->frequency_Hz, &index) != eLA_DATA_Success)
        return eLA_DATA_InvalidRecord;
    (void)LA_DATA_IndexToFrequency(index, &reference);
    if (reference != record->frequency_Hz) return eLA_DATA_InvalidRecord;
    LA_DATA_RecordToFields(record, fields);
    if (record->crc32 != LA_DATA_CalculateFieldsCrc(fields, DATA_RECORD_CRC_FIELD_COUNT)) return eLA_DATA_CrcMismatch;
    if (record->useCount == 0U)
    {
        for (unsigned i = eDATA_RecordField_Capacitance;
             i <= eDATA_RecordField_InductanceMax; ++i) if (fields[i] != 0U) return eLA_DATA_InvalidRecord;
    }
    else if (record->capacitanceMin_pF > record->capacitance_pF ||
        record->capacitance_pF > record->capacitanceMax_pF ||
        record->inductanceMin_nH > record->inductance_nH ||
        record->inductance_nH > record->inductanceMax_nH) return eLA_DATA_InvalidRecord;
    return eLA_DATA_Success;
}

/*
 * Builds an unused record for a specific frequency-grid slot.
 * INPUT: index: slot; record: non-NULL destination.
 * OUTPUT/RETURN: On Success, sets reference frequency, zero L/C/useCount and a valid CRC.
 * Invalid input leaves record unchanged. No record is written to storage.
 */
LA_DATA_Status_t LA_DATA_InitRecord(uint32_t index, LA_TUNING_Record_t *record)
{
    LA_TUNING_Record_t value = {0};
    LA_DATA_Status_t status;
    if (record == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_IndexToFrequency(index, &value.frequency_Hz);
    if (status != eLA_DATA_Success) return status;
    (void)LA_DATA_UpdateRecordCrc(&value);
    *record = value;
    return eLA_DATA_Success;
}


/*
 * Internal generic fixed-width numeric substitution into an XML template.
 * INPUT: pattern: NUL-terminated template; length: its stored length excluding NUL;
 * fields: ordered numeric array; count: element count; buffer/capacity: destination.
 * OUTPUT/RETURN: On Success, copies literals and replaces zero runs with zero-padded numbers,
 * decimal except the last CRC run (uppercase hex). Adds a C NUL; no FLASH write.
 * Checks capacity, field count and numeric overflow. Output may be partial on error.
 * This function ignores field semantics and never calculates CRC.
 */
static LA_DATA_Status_t LA_DATA_EncodeText(const char *pattern, size_t length,
    const uint32_t *fields, unsigned count, char *buffer, size_t capacity)
{
    unsigned field = 0U;
    if (buffer == NULL) return eLA_DATA_InvalidArgument;
    if (capacity < length + 1U) return eLA_DATA_BufferTooSmall;
    memcpy(buffer, pattern, length + 1U);
    for (size_t p = 0U; p < length;)
    {
        if (pattern[p] != '0') { ++p; continue; }
        if (field >= count) return eLA_DATA_InvalidFormat;
        size_t first = p;
        while (p < length && pattern[p] == '0') ++p;
        unsigned radix = field == count - 1U ? 16U : 10U;
        uint32_t value = fields[field++];
        for (size_t j = p; j > first;)
        {
            buffer[--j] = DATA_hexDigits[value % radix];
            value /= radix;
        }
        if (value != 0U) return eLA_DATA_Overflow;
    }
    return field == count ? eLA_DATA_Success : eLA_DATA_InvalidFormat;
}

/*
 * Internal generic extraction of numbers from the exact XML representation.
 * INPUT: pattern/expectedLength: protocol template; buffer/length: raw bytes;
 * fields/count: numeric output array and expected number of fields.
 * OUTPUT/RETURN: On Success, extracts ordered numbers; no terminating NUL required in input.
 * Rejects different literals/spacing, invalid digits, length or uint32 overflow.
 * The internal array may be partial on error; public callers publish only on success.
 */
static LA_DATA_Status_t LA_DATA_DecodeText(const char *pattern, size_t expectedLength,
    const char *buffer, size_t length, uint32_t *fields, unsigned count)
{
    unsigned field = 0U;
    if (buffer == NULL) return eLA_DATA_InvalidArgument;
    if (length != expectedLength) return eLA_DATA_InvalidFormat;
    for (size_t p = 0U; p < length;)
    {
        if (pattern[p] != '0')
        {
            if (buffer[p] != pattern[p]) return eLA_DATA_InvalidFormat;
            ++p;
            continue;
        }
        if (field >= count) return eLA_DATA_InvalidFormat;
        unsigned radix = field == count - 1U ? 16U : 10U;
        uint64_t value = 0U;
        while (p < length && pattern[p] == '0')
        {
            unsigned digit;
            char c = buffer[p++];
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (radix == 16U && c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10U;
            else return eLA_DATA_InvalidFormat;
            value = value * radix + digit;
            if (value > UINT32_MAX) return eLA_DATA_InvalidFormat;
        }
        fields[field++] = (uint32_t)value;
    }
    return field == count ? eLA_DATA_Success : eLA_DATA_InvalidFormat;
}

/*
 * Encodes one valid record as the fixed XML line.
 * INPUT: record: candidate with current CRC; buffer/capacity: caller output.
 * OUTPUT/RETURN: On Success, produces DATA_RECORD_XML_SIZE_BYTES plus C NUL.
 * Requires room for DATA_RECORD_XML_BUFFER_BYTES. No CRC update or storage access.
 */
LA_DATA_Status_t LA_DATA_EncodeRecordXML(const LA_TUNING_Record_t *record, char *buffer, size_t capacity)
{
    uint32_t fields[DATA_RECORD_FIELD_COUNT];
    LA_DATA_Status_t status = LA_DATA_CheckRecord(record);
    if (status != eLA_DATA_Success) return status;
    LA_DATA_RecordToFields(record, fields);
    return LA_DATA_EncodeText(DATA_recordTemplate, DATA_RECORD_XML_SIZE_BYTES, fields,
        DATA_RECORD_FIELD_COUNT, buffer, capacity);
}

/*
 * Decodes and validates one fixed XML record.
 * INPUT: buffer: raw record bytes; length: exact stored size; record: destination.
 * OUTPUT/RETURN: Publishes record only on Success. Checks syntax, grid, CRC and ranges.
 * Caller must additionally match the decoded frequency to the requested slot.
 */
LA_DATA_Status_t LA_DATA_DecodeRecordXML(const char *buffer, size_t length, LA_TUNING_Record_t *record)
{
    uint32_t fields[DATA_RECORD_FIELD_COUNT];
    LA_TUNING_Record_t value;
    LA_DATA_Status_t status;
    if (record == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_DecodeText(DATA_recordTemplate, DATA_RECORD_XML_SIZE_BYTES, buffer, length,
        fields, DATA_RECORD_FIELD_COUNT);
    if ( status != eLA_DATA_Success ) return status;
    LA_DATA_FieldsToRecord( fields, &value );
    status = LA_DATA_CheckRecord(&value);
    if (status == eLA_DATA_Success) *record = value;
    return status;
}

/*
 * Internal semantic mapping from header members to named enum indices.
 * INPUT: h: valid header; f: array of eDATA_HeaderField_Count elements.
 * OUTPUT/RETURN: Copies every member including CRC, without changing h.
 * Keeps field order explicit for shared generic CRC and text conversion.
 */
static void LA_DATA_HeaderToFields(const LA_DATA_Header_t *h, uint32_t f[DATA_HEADER_FIELD_COUNT])
{
    f[eDATA_HeaderField_Version] = h->version;
    f[eDATA_HeaderField_LoadId] = h->loadId;
    f[eDATA_HeaderField_FrequencyMin] = h->frequencyMin_Hz;
    f[eDATA_HeaderField_FrequencyMax] = h->frequencyMax_Hz;
    f[eDATA_HeaderField_FrequencyInterval] = h->frequencyInterval_Hz;
    f[eDATA_HeaderField_RecordCount] = h->recordCount;
    f[eDATA_HeaderField_RecordSize] = h->recordSize_bytes;
    f[eDATA_HeaderField_Crc] = h->crc32;
}

/*
 * Internal detailed validation of decoded metadata.
 * INPUT: header: candidate (NULL rejected).
 * OUTPUT/RETURN: Return: Success, CrcMismatch or IncompatibleHeader for version/grid/count/size.
 * Does not check the selected loadId, read FLASH or inspect stored record data.
 */
static LA_DATA_Status_t LA_DATA_CheckHeader(const LA_DATA_Header_t *header)
{
    uint32_t fields[DATA_HEADER_FIELD_COUNT];
    if (header == NULL) return eLA_DATA_InvalidArgument;
    LA_DATA_HeaderToFields(header, fields);
    if (header->crc32 != LA_DATA_CalculateFieldsCrc(fields, DATA_HEADER_CRC_FIELD_COUNT)) return eLA_DATA_CrcMismatch;
    if (header->version != DATA_FORMAT_VERSION || header->frequencyMin_Hz != DATA_FREQUENCY_MIN_HZ ||
        header->frequencyMax_Hz != DATA_FREQUENCY_MAX_HZ || header->frequencyInterval_Hz != DATA_FREQUENCY_INTERVAL_HZ ||
        header->recordCount != DATA_RECORD_COUNT ||
        (header->recordSize_bytes != DATA_RECORD_XML_SIZE_BYTES &&
         header->recordSize_bytes != DATA_RECORD_BINARY_SIZE_BYTES)) return eLA_DATA_IncompatibleHeader;
    return eLA_DATA_Success;
}

/*
 * Builds metadata for a selected descriptor and computes its CRC.
 * INPUT: database: configured descriptor; header: destination.
 * OUTPUT/RETURN: On Success, fills format version, loadId, grid, count, stored stride and CRC.
 * No database creation occurs. This is the logical header, not raw XML bytes.
 */
LA_DATA_Status_t LA_DATA_MakeHeader(const LA_DATA_Database_t *database, LA_DATA_Header_t *header)
{
    LA_DATA_Header_t value;
    LA_DATA_Layout_t layout;
    uint32_t fields[DATA_HEADER_FIELD_COUNT];
    LA_DATA_Status_t status;
    if (database == NULL || header == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_GetLayout(database->format, &layout);
    if (status != eLA_DATA_Success) return status;
    value.version = DATA_FORMAT_VERSION;
    value.loadId = database->loadId;
    value.frequencyMin_Hz = DATA_FREQUENCY_MIN_HZ;
    value.frequencyMax_Hz = DATA_FREQUENCY_MAX_HZ;
    value.frequencyInterval_Hz = DATA_FREQUENCY_INTERVAL_HZ;
    value.recordCount = layout.recordCount;
    value.recordSize_bytes = layout.storedRecordSize_bytes;
    value.crc32 = 0U;
    LA_DATA_HeaderToFields(&value, fields);
    value.crc32 = LA_DATA_CalculateFieldsCrc(fields, DATA_HEADER_CRC_FIELD_COUNT);
    *header = value;
    return eLA_DATA_Success;
}

/*
 * Encodes a valid XML-format header at the fixed byte length.
 * INPUT: header: metadata with valid CRC and XML record size; buffer/capacity: destination.
 * OUTPUT/RETURN: On Success, emits fixed header plus C NUL. Rejects incompatible/binary metadata.
 * Does not store it, create records or change the header CRC.
 */
LA_DATA_Status_t LA_DATA_EncodeHeaderXML(const LA_DATA_Header_t *header, char *buffer, size_t capacity)
{
    uint32_t fields[DATA_HEADER_FIELD_COUNT];
    LA_DATA_Status_t status;
    if (header == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_CheckHeader(header);
    if (status != eLA_DATA_Success) return status;
    if (header->recordSize_bytes != DATA_RECORD_XML_SIZE_BYTES) return eLA_DATA_InvalidFormat;
    LA_DATA_HeaderToFields(header, fields);
    return LA_DATA_EncodeText(DATA_headerTemplate, DATA_HEADER_XML_SIZE_BYTES, fields,
        DATA_HEADER_FIELD_COUNT, buffer, capacity);
}

/*
 * Internal inverse mapping using LA_DATA_HeaderField_t indices.
 * INPUT: fields: ordered array; header: destination.
 * OUTPUT/RETURN: Copies members without validation. Used after generic text/binary extraction.
 */
static void LA_DATA_FieldsToHeader(const uint32_t fields[DATA_HEADER_FIELD_COUNT], LA_DATA_Header_t *header)
{
    header->version = fields[eDATA_HeaderField_Version];
    header->loadId = fields[eDATA_HeaderField_LoadId];
    header->frequencyMin_Hz = fields[eDATA_HeaderField_FrequencyMin];
    header->frequencyMax_Hz = fields[eDATA_HeaderField_FrequencyMax];
    header->frequencyInterval_Hz = fields[eDATA_HeaderField_FrequencyInterval];
    header->recordCount = fields[eDATA_HeaderField_RecordCount];
    header->recordSize_bytes = fields[eDATA_HeaderField_RecordSize];
    header->crc32 = fields[eDATA_HeaderField_Crc];
}

/*
 * Internal syntax-only header extraction.
 * INPUT: buffer/length: exact fixed XML header; header: internal destination.
 * OUTPUT/RETURN: Extracts metadata using named enum indices but does not approve CRC or config.
 * Call CheckHeader before using offsets or claiming a valid database.
 */
static LA_DATA_Status_t LA_DATA_ParseHeaderXML(const char *buffer, size_t length, LA_DATA_Header_t *header)
{
    uint32_t fields[DATA_HEADER_FIELD_COUNT];
    LA_DATA_Status_t status = LA_DATA_DecodeText(DATA_headerTemplate, DATA_HEADER_XML_SIZE_BYTES,
        buffer, length, fields, DATA_HEADER_FIELD_COUNT);
    if (status == eLA_DATA_Success) LA_DATA_FieldsToHeader(fields, header);
    return status;
}

/*
 * Decodes and validates initial XML header bytes.
 * INPUT: buffer/length: exact header bytes; header: non-NULL destination.
 * OUTPUT/RETURN: Publishes only after syntax, CRC, version/grid/count and XML stride checks pass.
 * Does not compare loadId with a selected descriptor: use InspectDatabaseHeader.
 */
LA_DATA_Status_t LA_DATA_DecodeHeaderXML(const char *buffer, size_t length, LA_DATA_Header_t *header)
{
    LA_DATA_Header_t value;
    LA_DATA_Status_t status;
    if (header == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_ParseHeaderXML(buffer, length, &value);
    if (status != eLA_DATA_Success) return status;
    status = LA_DATA_CheckHeader(&value);
    if (status == eLA_DATA_Success && value.recordSize_bytes != DATA_RECORD_XML_SIZE_BYTES)
        return eLA_DATA_IncompatibleHeader;
    if (status == eLA_DATA_Success) *header = value;
    return status;
}

/*
 * Boolean approval of an already-decoded header.
 * INPUT: header: candidate metadata; NULL is invalid.
 * OUTPUT/RETURN: Return: true only for valid CRC and compatible version/grid/count/stride.
 * No FLASH read or load identity check. False does not mean the region is empty.
 */
bool LA_DATA_IsHeaderValid(const LA_DATA_Header_t *header)
{
    return LA_DATA_CheckHeader(header) == eLA_DATA_Success;
}

/*
 * Boolean approval of an already-decoded tuning record.
 * INPUT: record: candidate; NULL is invalid.
 * OUTPUT/RETURN: Return: true for valid reference frequency, CRC and L/C/useCount consistency.
 * A valid unused record is also true; test useCount to know whether tuning exists.
 */
bool LA_DATA_IsRecordValid(const LA_TUNING_Record_t *record)
{
    return LA_DATA_CheckRecord(record) == eLA_DATA_Success;
}

/*
 * Classifies initial storage bytes for one selected catalog descriptor.
 * INPUT: database: expected format/loadId; bytes/length: successfully read exact header;
 * state: required classification output; header: optional decoded destination.
 * OUTPUT/RETURN: Sets state to Unknown first. All erased bytes => Missing with Success.
 * Otherwise verifies syntax, CRC, metadata compatibility and selected loadId.
 * HeaderValid => Success and optional header published. Incompatible/InvalidHeader
 * preserve header output and return the detailed error. No reads/erase/writes occur.
 * Success alone is insufficient: inspect state; Missing is not HeaderValid.
 * Approval certifies the header only, never the complete database contents.
 */
LA_DATA_Status_t LA_DATA_InspectDatabaseHeader(const LA_DATA_Database_t *database,
    const uint8_t *bytes, size_t length, LA_DATA_DatabaseState_t *state, LA_DATA_Header_t *header)
{
    LA_DATA_Header_t value;
    LA_DATA_Status_t status;
    LA_DATA_Layout_t layout;
    bool erased = true;
    if (state == NULL) return eLA_DATA_InvalidArgument;
    *state = eLA_DATA_Database_Unknown;
    if (database == NULL || bytes == NULL) return eLA_DATA_InvalidArgument;
    status = LA_DATA_GetLayout(database->format, &layout);
    if (status != eLA_DATA_Success) return status;
    if (length != layout.headerSize_bytes) return eLA_DATA_InvalidArgument;
    for (size_t i = 0U; i < length; ++i)
        if (bytes[i] != DATA_FLASH_ERASED_BYTE) { erased = false; break; }
    if (erased)
    {
        *state = eLA_DATA_Database_Missing;
        return eLA_DATA_Success;
    }
    if (database->format == eLA_DATA_Format_XML)
        status = LA_DATA_ParseHeaderXML((const char *)bytes, length, &value);
    else
    {
        uint32_t fields[DATA_HEADER_FIELD_COUNT] = {0};
        for (unsigned i = 0U; i < DATA_HEADER_FIELD_COUNT; ++i)
            for (unsigned b = 0U; b < DATA_U32_BYTES; ++b)
                fields[i] |= (uint32_t)bytes[DATA_U32_BYTES * i + b] << (8U * b);
        LA_DATA_FieldsToHeader(fields, &value);
        status = eLA_DATA_Success;
    }
    if (status == eLA_DATA_Success) status = LA_DATA_CheckHeader(&value);
    if (status == eLA_DATA_Success &&
        (value.loadId != database->loadId || value.recordSize_bytes != layout.storedRecordSize_bytes))
        status = eLA_DATA_IncompatibleHeader;
    if (status == eLA_DATA_Success)
    {
        *state = eLA_DATA_Database_HeaderValid;
        if (header != NULL) *header = value;
    }
    else if (status == eLA_DATA_IncompatibleHeader) *state = eLA_DATA_Database_Incompatible;
    else *state = eLA_DATA_Database_InvalidHeader;
    return status;
}

/*
 * Serializes a valid record as explicit little-endian bytes.
 * INPUT: record: candidate with valid CRC; buffer/capacity: output storage.
 * OUTPUT/RETURN: Writes exactly DATA_RECORD_BINARY_SIZE_BYTES on Success; no C NUL.
 * Avoids native-struct layout/endianness dependence. No storage I/O.
 */
LA_DATA_Status_t LA_DATA_EncodeRecordBinary(const LA_TUNING_Record_t *record, uint8_t *buffer, size_t capacity)
{
    uint32_t fields[DATA_RECORD_FIELD_COUNT];
    LA_DATA_Status_t status;
    if (buffer == NULL) return eLA_DATA_InvalidArgument;
    if (capacity < DATA_RECORD_BINARY_SIZE_BYTES) return eLA_DATA_BufferTooSmall;
    status = LA_DATA_CheckRecord(record);
    if (status != eLA_DATA_Success) return status;
    LA_DATA_RecordToFields(record, fields);
    for (unsigned i = 0U; i < DATA_RECORD_FIELD_COUNT; ++i)
        for (unsigned b = 0U; b < DATA_U32_BYTES; ++b)
            buffer[DATA_U32_BYTES * i + b] = (uint8_t)(fields[i] >> (8U * b));
    return eLA_DATA_Success;
}

/*
 * Decodes and validates one serialized little-endian record.
 * INPUT: buffer/length: exact binary record; record: destination.
 * OUTPUT/RETURN: Publishes only after reference-grid, CRC and semantic checks pass.
 * Caller must still verify the record belongs to the requested slot.
 */
LA_DATA_Status_t LA_DATA_DecodeRecordBinary(const uint8_t *buffer, size_t length, LA_TUNING_Record_t *record)
{
    uint32_t fields[DATA_RECORD_FIELD_COUNT] = {0};
    LA_TUNING_Record_t value;
    LA_DATA_Status_t status;
    if (buffer == NULL || record == NULL) return eLA_DATA_InvalidArgument;
    if (length != DATA_RECORD_BINARY_SIZE_BYTES) return eLA_DATA_InvalidFormat;
    for (unsigned i = 0U; i < DATA_RECORD_FIELD_COUNT; ++i)
        for (unsigned b = 0U; b < DATA_U32_BYTES; ++b)
            fields[i] |= (uint32_t)buffer[DATA_U32_BYTES * i + b] << (8U * b);
    LA_DATA_FieldsToRecord(fields, &value);
    status = LA_DATA_CheckRecord(&value);
    if (status == eLA_DATA_Success) *record = value;
    return status;
}
