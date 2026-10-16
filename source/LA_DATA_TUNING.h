
#ifndef LA_DATA_TUNING_H_
#define LA_DATA_TUNING_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DATA_FREQUENCY_INTERVAL_HZ
#define DATA_FREQUENCY_INTERVAL_HZ   10000UL
#endif
#ifndef DATA_FREQUENCY_MIN_HZ
#define DATA_FREQUENCY_MIN_HZ        2000000UL
#endif
#ifndef DATA_FREQUENCY_MAX_HZ
#define DATA_FREQUENCY_MAX_HZ        30000000UL
#endif
#ifndef DATA_DATABASE_MAX_COUNT
#define DATA_DATABASE_MAX_COUNT      5U
#endif

#ifndef DATA_DATABASE_ALIGNMENT_BYTES
#define DATA_DATABASE_ALIGNMENT_BYTES 1UL
#endif
#ifndef DATA_FLASH_ERASED_BYTE
#define DATA_FLASH_ERASED_BYTE 0xFFU
#endif
#define DATA_FORMAT_VERSION          1UL
#define DATA_XML_DECIMAL_WIDTH       10U
#define DATA_XML_CRC_WIDTH           8U

#if DATA_FREQUENCY_INTERVAL_HZ == 0
#error DATA_FREQUENCY_INTERVAL_HZ must be nonzero
#endif
#if DATA_FREQUENCY_MAX_HZ < DATA_FREQUENCY_MIN_HZ
#error Invalid frequency range
#endif
#if DATA_FREQUENCY_MAX_HZ > UINT32_MAX
#error Frequencies must fit uint32_t
#endif
#if ((DATA_FREQUENCY_MAX_HZ - DATA_FREQUENCY_MIN_HZ) % DATA_FREQUENCY_INTERVAL_HZ) != 0
#error Band endpoints must align to DATA_FREQUENCY_INTERVAL_HZ
#endif
#if DATA_DATABASE_MAX_COUNT == 0 || DATA_DATABASE_ALIGNMENT_BYTES == 0
#error Database capacity and alignment must be nonzero
#endif

typedef char LA_DATA_ByteSizeCheck[(CHAR_BIT == 8) ? 1 : -1];

typedef enum LA_DATA_HeaderField_t
{
    eDATA_HeaderField_Version = 0,
    eDATA_HeaderField_LoadId,
    eDATA_HeaderField_FrequencyMin,
    eDATA_HeaderField_FrequencyMax,
    eDATA_HeaderField_FrequencyInterval,
    eDATA_HeaderField_RecordCount,
    eDATA_HeaderField_RecordSize,
    eDATA_HeaderField_Crc,
    eDATA_HeaderField_Count
} LA_DATA_HeaderField_t;

typedef enum LA_DATA_RecordField_t
{
    eDATA_RecordField_Frequency = 0,
    eDATA_RecordField_Capacitance,
    eDATA_RecordField_CapacitanceMin,
    eDATA_RecordField_CapacitanceMax,
    eDATA_RecordField_Inductance,
    eDATA_RecordField_InductanceMin,
    eDATA_RecordField_InductanceMax,
    eDATA_RecordField_UseCount,
    eDATA_RecordField_Crc,
    eDATA_RecordField_Count
} LA_DATA_RecordField_t;

typedef enum LA_DATA_DatabaseState_t
{
    eLA_DATA_Database_Unknown = 0,
    eLA_DATA_Database_Missing,
    eLA_DATA_Database_HeaderValid,
    eLA_DATA_Database_InvalidHeader,
    eLA_DATA_Database_Incompatible,
    eLA_DATA_Database_ReadError
} LA_DATA_DatabaseState_t;

typedef struct LA_TUNING_Record_t
{
    uint32_t frequency_Hz;
    uint32_t capacitance_pF;
    uint32_t capacitanceMin_pF;
    uint32_t capacitanceMax_pF;
    uint32_t inductance_nH;
    uint32_t inductanceMin_nH;
    uint32_t inductanceMax_nH;
    uint32_t useCount;
    uint32_t crc32;
} LA_TUNING_Record_t;

typedef char LA_TUNING_Record_SizeCheck[
    (sizeof(LA_TUNING_Record_t) == 36U) ? 1 : -1];

typedef struct LA_DATA_Header_t
{
    uint32_t version;
    uint32_t loadId;
    uint32_t frequencyMin_Hz;
    uint32_t frequencyMax_Hz;
    uint32_t frequencyInterval_Hz;
    uint32_t recordCount;
    uint32_t recordSize_bytes;
    uint32_t crc32;
} LA_DATA_Header_t;

typedef char LA_DATA_Header_SizeCheck[
    (sizeof(LA_DATA_Header_t) == 32U) ? 1 : -1];

typedef enum LA_DATA_Status_t
{
    eLA_DATA_Success = 0,
    eLA_DATA_InvalidArgument,
    eLA_DATA_FrequencyOutOfRange,
    eLA_DATA_IndexOutOfRange,
    eLA_DATA_Overflow,
    eLA_DATA_BufferTooSmall,
    eLA_DATA_InvalidFormat,
    eLA_DATA_CrcMismatch,
    eLA_DATA_InvalidRecord,
    eLA_DATA_IncompatibleHeader,
    eLA_DATA_StorageError,
    eLA_DATA_VerificationFailed
} LA_DATA_Status_t;

typedef enum LA_DATA_Format_t
{
    eLA_DATA_Format_Binary = 0,
    eLA_DATA_Format_XML = 1
} LA_DATA_Format_t;

/*
 * LA_DATA_Database_t - Descriptor of one tuning database.
 *
 * Identifies the database associated with one external load and describes
 * its storage representation and logical starting position.
 *
 * This structure does not contain tuning records, cached data, a file handle
 * or information certifying that the database exists in storage.
 *
 * loadId:
 *     Stable identifier assigned by the application to the external load.
 *     Must be unique within the catalog.
 *     The same identifier is stored in the database header and checked when
 *     inspecting that header, preventing selection of another load's database.
 *     It is neither a frequency nor an electrical measurement of the load.
 *
 * format:
 *     Stored representation: eLA_DATA_Format_Binary or eLA_DATA_Format_XML.
 *     Determines header size, record stride, footer size and total byte length.
 *
 * baseOffset_bytes:
 *     Logical byte address of the first byte of the database header.
 *     For a raw FLASH partition or a shared container, this is the base used
 *     to calculate absolute record addresses.
 *     It is not a pointer into MCU RAM.
 *
 *     When each database is a separate filesystem file, record access normally
 *     uses relative file offsets instead. A filename/file-handle association
 *     must be supplied by the future filesystem adapter.
 *
 * Initializing this descriptor does not read, erase or create the database.
 * Existence and compatibility must be checked separately from stored bytes.
 */
typedef struct LA_DATA_Database_t
{
    uint32_t loadId;
    LA_DATA_Format_t format;
    uint64_t baseOffset_bytes;
} LA_DATA_Database_t;



/*
 * LA_DATA_Catalog_t - Small table of configured database descriptors.
 *
 * Describes which external loads have database instances and where those
 * instances are logically located. It does not contain their record arrays.
 *
 * databaseCount:
 *     Actual number of configured entries.
 *     Valid initialized catalogs contain 1..DATA_DATABASE_MAX_COUNT entries.
 *     Only databases[0] through databases[databaseCount - 1] are active.
 *
 * databases:
 *     Fixed-capacity array of LA_DATA_Database_t descriptors.
 *     DATA_DATABASE_MAX_COUNT sets its compile-time capacity, not the number
 *     of databases that already exist in FLASH.
 *     Entries beyond databaseCount must not be used.
 *
 * Each entry has an independent existence/validity state:
 * one database can be present while another is missing or incompatible.
 * The catalog itself does not store these startup results.
 *
 * LA_DATA_InitCatalog() assigns unique load IDs and consecutive logical
 * reservations according to the selected format and configured alignment.
 * LA_DATA_FindDatabase() searches active entries by loadId.
 *
 * The catalog occupies RAM only for its descriptors. Record contents remain
 * in external storage and are read individually when required.
 *
 * Changing the catalog does not move, migrate or erase existing databases.
 */
typedef struct LA_DATA_Catalog_t
{
    uint32_t databaseCount;
    LA_DATA_Database_t databases[DATA_DATABASE_MAX_COUNT];
} LA_DATA_Catalog_t;

/*
 * LA_DATA_Layout_t - Calculated dimensions of one database representation.
 *
 * Produced by LA_DATA_GetLayout() from the configured frequency grid,
 * selected format and logical reservation alignment.
 *
 * Describes sizes only. It does not identify an external load, contain a
 * storage base address, or certify that a database exists.
 *
 * recordCount:
 *     Number of frequency-reference slots in one database.
 *     Includes both configured band endpoints.
 *
 * binaryRecordSize_bytes:
 *     Binary size of one complete tuning record, including its CRC.
 *     Version 1 uses 36 bytes, independently of the selected storage format.
 *
 * storedRecordSize_bytes:
 *     Actual byte stride of each record in the selected representation.
 *     Version 1: 36 bytes for binary, 124 bytes for fixed-width XML.
 *     XML length excludes the terminating NUL used in C buffers.
 *
 * headerSize_bytes:
 *     Number of stored bytes preceding the first record.
 *     Version 1: 32 bytes for binary, 149 bytes for XML.
 *
 * footerSize_bytes:
 *     Number of stored bytes following the final record.
 *     Zero for binary; 12 bytes for the XML closing element and LF.
 *
 * binaryPayloadSize_bytes:
 *     recordCount * binaryRecordSize_bytes.
 *     Size of the record array in binary representation, even when XML
 *     storage is selected. Excludes header, footer and padding.
 *
 * storedPayloadSize_bytes:
 *     recordCount * storedRecordSize_bytes.
 *     Actual stored record-array length for the selected format.
 *     Excludes header, footer and padding.
 *
 * databaseSize_bytes:
 *     headerSize_bytes + storedPayloadSize_bytes + footerSize_bytes.
 *     Exact logical database/file length, without reservation padding.
 *
 * allocationSize_bytes:
 *     databaseSize_bytes rounded upward to a multiple of
 *     DATA_DATABASE_ALIGNMENT_BYTES.
 *     Used as the stride between consecutive database reservations.
 *     Padding lies after the database and is not part of its XML document.
 *
 * These dimensions exclude filesystem metadata, journals, redundant copies
 * and any additional space required by the physical FLASH implementation.
 * Logical alignment alone does not implement sector preservation or recovery.
 */
typedef struct LA_DATA_Layout_t
{
    uint32_t recordCount;
    uint32_t binaryRecordSize_bytes;
    uint32_t storedRecordSize_bytes;
    uint32_t headerSize_bytes;
    uint32_t footerSize_bytes;
    uint64_t binaryPayloadSize_bytes;
    uint64_t storedPayloadSize_bytes;
    uint64_t databaseSize_bytes;
    uint64_t allocationSize_bytes;
} LA_DATA_Layout_t;

/*
 * LA_DATA_RecordLocation_t - Address and frequency description of one slot.
 *
 * Produced by LA_DATA_GetRecordLocation() from a database descriptor and
 * a requested frequency. Contains the information needed to fetch a record,
 * not the tuning values stored in that record.
 *
 * index:
 *     Zero-based record index within the selected database.
 *     Calculated using the lower-reference-slot policy:
 *         (requestedFrequency - minimumFrequency) / frequencyInterval.
 *     Frequencies outside the configured band are rejected.
 *
 * referenceFrequency_Hz:
 *     Frequency assigned to the selected slot:
 *         minimumFrequency + index * frequencyInterval.
 *     May differ from the requested frequency.
 *     After decoding a stored record, its frequency_Hz must match this value.
 *
 * intervalFirst_Hz:
 *     First requested frequency assigned to the slot, inclusive.
 *     Equal to referenceFrequency_Hz.
 *
 * intervalLast_Hz:
 *     Last requested frequency assigned to the slot, inclusive.
 *     Normally referenceFrequency_Hz + frequencyInterval - 1.
 *     The final slot represents only the configured maximum frequency,
 *     so its first and last frequencies are equal.
 *
 * recordSize_bytes:
 *     Number of bytes to read/write for this record in the selected format.
 *     Excludes a C-string terminating NUL.
 *
 * relativeOffset_bytes:
 *     Record position measured from the beginning of its database:
 *         headerSize + index * storedRecordSize.
 *     Suitable for seeking within an individual database file.
 *
 * absoluteOffset_bytes:
 *     database.baseOffset_bytes + relativeOffset_bytes.
 *     Suitable for logical addressing in raw FLASH or a shared container.
 *     This is a byte address, not an MCU pointer or a physical-sector number.
 *
 * Successful location calculation proves only that the request fits the
 * configured layout and that address arithmetic did not overflow.
 * It does not verify actual FLASH capacity, database existence or record CRC.
 * Those checks belong to the storage-access and decoding steps.
 */
typedef struct LA_DATA_RecordLocation_t
{
    uint32_t index;
    uint32_t referenceFrequency_Hz;
    uint32_t intervalFirst_Hz;
    uint32_t intervalLast_Hz;  
    uint32_t recordSize_bytes;
    uint64_t relativeOffset_bytes;
    uint64_t absoluteOffset_bytes;
} LA_DATA_RecordLocation_t;

#define DATA_RECORD_COUNT \
    ((uint64_t)(DATA_FREQUENCY_MAX_HZ - DATA_FREQUENCY_MIN_HZ) \
        / DATA_FREQUENCY_INTERVAL_HZ + UINT64_C(1))
#define DATA_RECORD_BINARY_SIZE_BYTES ((uint32_t)sizeof(LA_TUNING_Record_t))
#define DATA_HEADER_BINARY_SIZE_BYTES ((uint32_t)sizeof(LA_DATA_Header_t))

#define DATA_XML_RECORD_TEMPLATE \
    "<r f=\"0000000000\" c=\"0000000000 0000000000 0000000000\" " \
    "l=\"0000000000 0000000000 0000000000\" n=\"0000000000\" crc=\"00000000\"/>\n"
#define DATA_XML_HEADER_TEMPLATE \
    "<database v=\"0000000000\" load=\"0000000000\" fmin=\"0000000000\" " \
    "fmax=\"0000000000\" step=\"0000000000\" count=\"0000000000\" " \
    "rec=\"0000000000\" crc=\"00000000\">\n"
#define DATA_XML_FOOTER "</database>\n"
#define DATA_RECORD_XML_SIZE_BYTES ((uint32_t)(sizeof(DATA_XML_RECORD_TEMPLATE) - 1U))
#define DATA_HEADER_XML_SIZE_BYTES ((uint32_t)(sizeof(DATA_XML_HEADER_TEMPLATE) - 1U))
#define DATA_FOOTER_XML_SIZE_BYTES ((uint32_t)(sizeof(DATA_XML_FOOTER) - 1U))
#define DATA_RECORD_XML_BUFFER_BYTES (DATA_RECORD_XML_SIZE_BYTES + 1U)
#define DATA_HEADER_XML_BUFFER_BYTES (DATA_HEADER_XML_SIZE_BYTES + 1U)

#define DATA_DATABASE_BINARY_PAYLOAD_BYTES \
    (DATA_RECORD_COUNT * DATA_RECORD_BINARY_SIZE_BYTES)
#define DATA_DATABASE_BINARY_SIZE_BYTES \
    (DATA_HEADER_BINARY_SIZE_BYTES + DATA_DATABASE_BINARY_PAYLOAD_BYTES)
#define DATA_DATABASE_XML_PAYLOAD_BYTES \
    (DATA_RECORD_COUNT * DATA_RECORD_XML_SIZE_BYTES)
#define DATA_DATABASE_XML_SIZE_BYTES \
    (DATA_HEADER_XML_SIZE_BYTES + DATA_DATABASE_XML_PAYLOAD_BYTES + DATA_FOOTER_XML_SIZE_BYTES)
#define DATA_DATABASE_XML_ALLOCATION_BYTES \
    (((DATA_DATABASE_XML_SIZE_BYTES + DATA_DATABASE_ALIGNMENT_BYTES - UINT64_C(1)) \
        / DATA_DATABASE_ALIGNMENT_BYTES) * DATA_DATABASE_ALIGNMENT_BYTES)
#define DATA_ALL_DATABASES_XML_ALLOCATION_BYTES \
    (DATA_DATABASE_MAX_COUNT * DATA_DATABASE_XML_ALLOCATION_BYTES)

typedef char LA_DATA_RecordCountCheck[(DATA_RECORD_COUNT <= UINT32_MAX) ? 1 : -1];

uint32_t LA_DATA_GetRecordBinarySize(void);
uint64_t LA_DATA_GetDatabaseBinarySize(void);
uint64_t LA_DATA_GetDatabaseXMLSize(void);
LA_DATA_Status_t LA_DATA_GetLayout(LA_DATA_Format_t format, LA_DATA_Layout_t *layout);

LA_DATA_Status_t LA_DATA_FrequencyToIndex(uint32_t frequency_Hz, uint32_t *index);
LA_DATA_Status_t LA_DATA_IndexToFrequency(uint32_t index, uint32_t *frequency_Hz);
LA_DATA_Status_t LA_DATA_GetRecordOffsetByIndex(LA_DATA_Format_t format,
    uint32_t index, uint64_t *relativeOffset_bytes);
LA_DATA_Status_t LA_DATA_GetRecordOffsetByFrequency(LA_DATA_Format_t format,
    uint32_t frequency_Hz, uint64_t *relativeOffset_bytes);
LA_DATA_Status_t LA_DATA_GetRecordLocation(const LA_DATA_Database_t *database,
    uint32_t frequency_Hz, LA_DATA_RecordLocation_t *location);

LA_DATA_Status_t LA_DATA_InitCatalog(LA_DATA_Catalog_t *catalog, uint32_t count,
    const uint32_t *loadIds, LA_DATA_Format_t format, uint64_t firstBase_bytes);
const LA_DATA_Database_t *LA_DATA_FindDatabase(const LA_DATA_Catalog_t *catalog,
    uint32_t loadId);
LA_DATA_Status_t LA_DATA_GetCatalogAllocatedSize(const LA_DATA_Catalog_t *catalog,
    uint64_t *size_bytes);

uint32_t LA_DATA_CalculateCrc32(const void *bytes, size_t count);
LA_DATA_Status_t LA_DATA_UpdateRecordCrc(LA_TUNING_Record_t *record);
bool LA_DATA_IsRecordValid(const LA_TUNING_Record_t *record);
LA_DATA_Status_t LA_DATA_InitRecord(uint32_t index, LA_TUNING_Record_t *record);

LA_DATA_Status_t LA_DATA_EncodeRecordXML(const LA_TUNING_Record_t *record,
    char *buffer, size_t capacity);
LA_DATA_Status_t LA_DATA_DecodeRecordXML(const char *buffer, size_t length,
    LA_TUNING_Record_t *record);
bool LA_DATA_IsHeaderValid(const LA_DATA_Header_t *header);
LA_DATA_Status_t LA_DATA_InspectDatabaseHeader(const LA_DATA_Database_t *database,
    const uint8_t *bytes, size_t length, LA_DATA_DatabaseState_t *state,
    LA_DATA_Header_t *header);
LA_DATA_Status_t LA_DATA_MakeHeader(const LA_DATA_Database_t *database,
    LA_DATA_Header_t *header);
LA_DATA_Status_t LA_DATA_EncodeHeaderXML(const LA_DATA_Header_t *header,
    char *buffer, size_t capacity);
LA_DATA_Status_t LA_DATA_DecodeHeaderXML(const char *buffer, size_t length,
    LA_DATA_Header_t *header);

LA_DATA_Status_t LA_DATA_EncodeRecordBinary(const LA_TUNING_Record_t *record,
    uint8_t *buffer, size_t capacity);
LA_DATA_Status_t LA_DATA_DecodeRecordBinary(const uint8_t *buffer, size_t length,
    LA_TUNING_Record_t *record);

#ifdef __cplusplus
}
#endif
#endif 
