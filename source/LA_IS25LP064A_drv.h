#ifndef LA_IS25LP064A_DRV_H_
#define LA_IS25LP064A_DRV_H_
#include "SPIS_drv.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef IS25LP064A_SPI_BASE
#define IS25LP064A_SPI_BASE SPI0
#endif
#ifndef IS25LP064A_SPI_TRANSFER_FLAGS
#define IS25LP064A_SPI_TRANSFER_FLAGS \
    (kDSPI_MasterCtar0 | kDSPI_MasterPcs4 | kDSPI_MasterPcsContinuous)
#endif
#ifndef IS25LP064A_SPI_BAUDRATE_HZ
#define IS25LP064A_SPI_BAUDRATE_HZ 5000000UL
#endif
#define IS25LP064A_SPI_DELAY_NS 1000UL
#define IS25LP064A_CAPACITY_BYTES 0x00800000UL
#define IS25LP064A_PAGE_SIZE_BYTES 256UL
#define IS25LP064A_SECTOR_SIZE_BYTES 4096UL
#define IS25LP064A_SECTOR_COUNT (IS25LP064A_CAPACITY_BYTES / IS25LP064A_SECTOR_SIZE_BYTES)
#define IS25LP064A_JEDEC_ID 0x009D6017UL
#define IS25LP064A_TRANSFER_BUFFER_BYTES (4U + IS25LP064A_PAGE_SIZE_BYTES)
#define IS25LP064A_READY_TIMEOUT_MS 60000UL
#define IS25LP064A_PROGRAM_TIMEOUT_MS 10UL
#define IS25LP064A_SECTOR_TIMEOUT_MS 1000UL
#define IS25LP064A_BLOCK32_TIMEOUT_MS 2000UL
#define IS25LP064A_BLOCK64_TIMEOUT_MS 3000UL
#define IS25LP064A_CHIP_TIMEOUT_MS 60000UL
#define IS25LP064A_REGISTER_TIMEOUT_MS 100UL

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IS25LP064A_status_t
{
    eIS25LP064A_Success = 0,
    eIS25LP064A_InvalidArgument,
    eIS25LP064A_NotInitialized,
    eIS25LP064A_Busy,
    eIS25LP064A_Timeout,
    eIS25LP064A_SPIError,
    eIS25LP064A_WrongDevice,
    eIS25LP064A_WriteProtected,
    eIS25LP064A_WriteEnableFailed,
    eIS25LP064A_RequiresErase,
    eIS25LP064A_VerificationFailed,
    eIS25LP064A_Suspended,
    eIS25LP064A_InvalidExecutionContext
} IS25LP064A_status_t;

typedef union IS25LP064A_STATUS_t
{
    uint8_t b8;
    struct __attribute__((packed))
    {
        uint8_t WIP : 1;
        uint8_t WEL : 1;
        uint8_t BP : 4;
        uint8_t QE : 1;
        uint8_t SRWD : 1;
    } bits;
} IS25LP064A_STATUS_t;

typedef union IS25LP064A_FUNCTION_t
{
    uint8_t b8;
    struct __attribute__((packed))
    {
        uint8_t sp0 : 1;
        uint8_t TBS : 1;
        uint8_t PSUS : 1;
        uint8_t ESUS : 1;
        uint8_t IRL0 : 1;
        uint8_t IRL1 : 1;
        uint8_t IRL2 : 1;
        uint8_t IRL3 : 1;
    } bits;
} IS25LP064A_FUNCTION_t;

typedef char IS25LP064A_STATUS_SizeCheck[(sizeof(IS25LP064A_STATUS_t) == 1U) ? 1 : -1];
typedef char IS25LP064A_FUNCTION_SizeCheck[(sizeof(IS25LP064A_FUNCTION_t) == 1U) ? 1 : -1];

typedef uint32_t (*IS25LP064A_getTimeMs_t)(void *userData);
typedef void (*IS25LP064A_idle_t)(void *userData);

typedef struct IS25LP064A_config_t
{
    SPI_Type *base;
    uint32_t transferFlags;
    uint32_t baudrate_Hz;
    IS25LP064A_getTimeMs_t getTimeMs;
    IS25LP064A_idle_t idle;
    void *userData;
} IS25LP064A_config_t;

typedef struct IS25LP064A_descriptor_t
{
    uint32_t jedecId;
    uint32_t capacity_bytes;
    uint32_t pageSize_bytes;
    uint32_t sectorSize_bytes;
    uint32_t sectorCount;
    uint8_t addressByteCount;
} IS25LP064A_descriptor_t;

typedef struct IS25LP064A_startupLog_t
{
    IS25LP064A_status_t result;
    uint32_t jedecId;
    IS25LP064A_STATUS_t status;
    IS25LP064A_FUNCTION_t function;
} IS25LP064A_startupLog_t;

typedef struct IS25LP064A_operationLog_t
{
    IS25LP064A_status_t result;
    status_t spiStatus;
    uint32_t address;
    uint32_t requestedBytes;
    uint32_t completedBytes;
} IS25LP064A_operationLog_t;

typedef struct IS25LP064A_device_t
{
    IS25LP064A_config_t config;
    IS25LP064A_descriptor_t descriptor;
    IS25LP064A_startupLog_t startupLog;
    IS25LP064A_operationLog_t operationLog;
    volatile bool busy;
    bool initialized;
    uint8_t txData[IS25LP064A_TRANSFER_BUFFER_BYTES];
    uint8_t rxData[IS25LP064A_TRANSFER_BUFFER_BYTES];
} IS25LP064A_device_t;

void IS25LP064A_GetDefaultConfig(IS25LP064A_config_t *config);
IS25LP064A_status_t IS25LP064A_InitContext(IS25LP064A_device_t *device,
    const IS25LP064A_config_t *config);
IS25LP064A_status_t IS25LP064A_StartBlocking(IS25LP064A_device_t *device);
IS25LP064A_status_t IS25LP064A_ReadIdBlocking(IS25LP064A_device_t *device, uint32_t *jedecId);
IS25LP064A_status_t IS25LP064A_ReadStatusBlocking(IS25LP064A_device_t *device,
    IS25LP064A_STATUS_t *status);
IS25LP064A_status_t IS25LP064A_ReadFunctionBlocking(IS25LP064A_device_t *device,
    IS25LP064A_FUNCTION_t *function);
IS25LP064A_status_t IS25LP064A_WaitReadyBlocking(IS25LP064A_device_t *device, uint32_t timeout_ms);
IS25LP064A_status_t IS25LP064A_ReadBlocking(IS25LP064A_device_t *device,
    uint32_t address, void *buffer, uint32_t length);
IS25LP064A_status_t IS25LP064A_VerifyBlocking(IS25LP064A_device_t *device,
    uint32_t address, const void *expected, uint32_t length);
IS25LP064A_status_t IS25LP064A_ProgramBlocking(IS25LP064A_device_t *device,
    uint32_t address, const void *data, uint32_t length);
IS25LP064A_status_t IS25LP064A_UpdateBlocking(IS25LP064A_device_t *device,
    uint32_t address, const void *data, uint32_t length,
    void *sectorBuffer, size_t sectorBufferSize);
IS25LP064A_status_t IS25LP064A_EraseSectorBlocking(IS25LP064A_device_t *device,
    uint32_t address);
IS25LP064A_status_t IS25LP064A_EraseBlock32Blocking(IS25LP064A_device_t *device,
    uint32_t address);
IS25LP064A_status_t IS25LP064A_EraseBlock64Blocking(IS25LP064A_device_t *device,
    uint32_t address);
IS25LP064A_status_t IS25LP064A_EraseRangeBlocking(IS25LP064A_device_t *device,
    uint32_t address, uint32_t length);
IS25LP064A_status_t IS25LP064A_EraseChipBlocking(IS25LP064A_device_t *device);
IS25LP064A_status_t IS25LP064A_ClearBlockProtectionBlocking(IS25LP064A_device_t *device);

#ifdef __cplusplus
}
#endif
#endif
