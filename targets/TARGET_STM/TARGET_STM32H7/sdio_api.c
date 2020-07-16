/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "sdio_api.h"
#include "platform/mbed_error.h"

#if DEVICE_SDIO
#define CMD_TIMEOUT 30000
/* Extern variables ---------------------------------------------------------*/

SD_HandleTypeDef hsd;
DMA_HandleTypeDef hdma_sdmmc_rx;
DMA_HandleTypeDef hdma_sdmmc_tx;

// simple flags for DMA pending signaling
volatile int SD_DMA_ReadPendingState = SD_TRANSFER_OK;
volatile int SD_DMA_WritePendingState = SD_TRANSFER_OK;

/* DMA Handlers are global, there is only one SDIO interface */

/**
* @brief This function handles SDMMC global interrupt.
*/
void _SDMMC_IRQHandler(void)
{
    HAL_SD_IRQHandler(&hsd);
}

/**
* @brief This function handles DMAx stream_n global interrupt. DMA Rx
*/
void _DMA_Stream_Rx_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_sdmmc_rx);
}

/**
* @brief This function handles DMAx stream_n global interrupt. DMA Tx
*/
void _DMA_Stream_Tx_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_sdmmc_tx);
}

void HAL_SD_MspInit(SD_HandleTypeDef* sdHandle)
{

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(sdHandle->Instance==SDMMC1)
    {
        /* SDMMC1 clock enable */
        __HAL_RCC_SDMMC1_CLK_ENABLE();
    
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        /**SDMMC1 GPIO Configuration    
        PC8     ------> SDMMC1_D0
        PC9     ------> SDMMC1_D1
        PC10     ------> SDMMC1_D2
        PC11     ------> SDMMC1_D3
        PC12     ------> SDMMC1_CK
        PD2     ------> SDMMC1_CMD 
        */
        GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF12_SDIO1;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_2;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF12_SDIO1;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    }
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* sdHandle)
{

    if(sdHandle->Instance==SDMMC1)
    {
        /* Peripheral clock disable */
        __HAL_RCC_SDMMC1_CLK_DISABLE();
    
        /**SDMMC1 GPIO Configuration    
        PC8     ------> SDMMC1_D0
        PC9     ------> SDMMC1_D1
        PC10     ------> SDMMC1_D2
        PC11     ------> SDMMC1_D3
        PC12     ------> SDMMC1_CK
        PD2     ------> SDMMC1_CMD 
        */
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12);
        HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
    }
}

/**
 * @brief  DeInitializes the SD MSP.
 * @param  hsd: SD handle
 * @param  Params : pointer on additional configuration parameters, can be NULL.
 */
__weak void SD_MspDeInit(SD_HandleTypeDef *hsd, void *Params)
{
    static DMA_HandleTypeDef dma_rx_handle;
    static DMA_HandleTypeDef dma_tx_handle;

    /* Disable NVIC for DMA transfer complete interrupts */
    HAL_NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Stream6_IRQn);

    /* Deinitialize the stream for new transfer */
    dma_rx_handle.Instance = DMA2_Stream3;
    HAL_DMA_DeInit(&dma_rx_handle);

    /* Deinitialize the stream for new transfer */
    dma_tx_handle.Instance = DMA2_Stream6;
    HAL_DMA_DeInit(&dma_tx_handle);

    /* Disable NVIC for SDMMC interrupts */
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);

    /* Disable SDMMC clock */
    __HAL_RCC_SDMMC1_CLK_DISABLE();
}

/**
  * @brief  Enables the SD wide bus mode.
  * @param  hsd pointer to SD handle
  * @retval error state
  */
static uint32_t SD_WideBus_Enable(SD_HandleTypeDef *hsd)
{
    uint32_t errorstate = HAL_SD_ERROR_NONE;

    if ((SDMMC_GetResponse(hsd->Instance, SDMMC_RESP1) & SDMMC_CARD_LOCKED) == SDMMC_CARD_LOCKED)
    {
        return HAL_SD_ERROR_LOCK_UNLOCK_FAILED;
    }

    /* Send CMD55 APP_CMD with argument as card's RCA.*/
    errorstate = SDMMC_CmdAppCommand(hsd->Instance, (uint32_t)(hsd->SdCard.RelCardAdd << 16U));
    if (errorstate != HAL_OK)
    {
        return errorstate;
    }

    /* Send ACMD6 APP_CMD with argument as 2 for wide bus mode */
    errorstate = SDMMC_CmdBusWidth(hsd->Instance, 2U);
    if (errorstate != HAL_OK)
    {
        return errorstate;
    }

    hsd->Init.BusWide = SDMMC_BUS_WIDE_4B;
    SDMMC_Init(hsd->Instance, hsd->Init);

    return HAL_SD_ERROR_NONE;
}

int sdio_init(void)
{
    int sd_state = MSD_OK;

    hsd.Instance = SDMMC1;
    hsd.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv = 0;
    hsd.Init.TranceiverPresent = SDMMC_TRANSCEIVER_NOT_PRESENT;

    /* HAL SD initialization */
    sd_state = HAL_SD_Init(&hsd);
    /* Configure SD Bus width (4 bits mode selected) */
    if (sd_state == MSD_OK)
    {
        /* Enable wide operation */
        if (SD_WideBus_Enable(&hsd) != HAL_OK)
        {
            sd_state = MSD_ERROR;
        }
    }

    return sd_state;
}

int sdio_deinit(void)
{
    int sd_state = MSD_OK;

    hsd.Instance = SDMMC1;

    /* HAL SD deinitialization */
    if (HAL_SD_DeInit(&hsd) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    /* Msp SD deinitialization */
    hsd.Instance = SDMMC1;
    SD_MspDeInit(&hsd, NULL);

    return sd_state;
}

int sdio_read_blocks(uint8_t *data, uint32_t address, uint32_t block_count)
{
    int sd_state = MSD_OK;

    if (HAL_SD_ReadBlocks(&hsd, data, address, block_count, CMD_TIMEOUT) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

int sdio_write_blocks(uint8_t *data, uint32_t address, uint32_t block_count)
{
    int sd_state = MSD_OK;

    if (HAL_SD_WriteBlocks(&hsd, data, address, block_count, CMD_TIMEOUT) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

int sdio_read_blocks_async(uint8_t *data, uint32_t address, uint32_t block_count)
{
    int sd_state = MSD_OK;
    SD_DMA_ReadPendingState = SD_TRANSFER_BUSY;

    /* Read block(s) in DMA transfer mode */
    if (HAL_SD_ReadBlocks_DMA(&hsd, data, address, block_count) != HAL_OK)
    {
        sd_state = MSD_ERROR;
        SD_DMA_ReadPendingState = SD_TRANSFER_OK;
    }

    return sd_state;
}

int sdio_write_blocks_async(uint8_t *data, uint32_t address, uint32_t block_count)
{
    int sd_state = MSD_OK;
    SD_DMA_WritePendingState = SD_TRANSFER_BUSY;

    /* Write block(s) in DMA transfer mode */
    if (HAL_SD_WriteBlocks_DMA(&hsd, data, address, block_count) != HAL_OK)
    {
        sd_state = MSD_ERROR;
        SD_DMA_WritePendingState = SD_TRANSFER_OK;
    }

    return sd_state;
}

int sdio_erase(uint32_t start_address, uint32_t end_address)
{
    int sd_state = MSD_OK;

    if (HAL_SD_Erase(&hsd, start_address, end_address) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

int sdio_get_card_state(void)
{
    return ((HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) ? SD_TRANSFER_OK : SD_TRANSFER_BUSY);
}

void sdio_get_card_info(sdio_card_info_t *card_info)
{
    /* Get SD card Information, copy structure for portability */
    HAL_SD_CardInfoTypeDef HAL_CardInfo;

    HAL_SD_GetCardInfo(&hsd, &HAL_CardInfo);

    if (card_info)
    {
        card_info->card_type = HAL_CardInfo.CardType;
        card_info->card_version = HAL_CardInfo.CardVersion;
        card_info->card_class = HAL_CardInfo.Class;
        card_info->rel_card_addr = HAL_CardInfo.RelCardAdd;
        card_info->block_count = HAL_CardInfo.BlockNbr;
        card_info->block_size = HAL_CardInfo.BlockSize;
        card_info->log_block_count = HAL_CardInfo.LogBlockNbr;
        card_info->log_block_size = HAL_CardInfo.LogBlockSize;
    }
}

int sdio_read_pending(void)
{
    return SD_DMA_ReadPendingState;
}

int sdio_write_pending(void)
{
    return SD_DMA_WritePendingState;
}

/**
  * @brief Rx Transfer completed callbacks
  * @param hsd Pointer SD handle
  * @retval None
  */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    SD_DMA_ReadPendingState = SD_TRANSFER_OK;
}

/**
  * @brief Tx Transfer completed callbacks
  * @param hsd Pointer to SD handle
  * @retval None
  */
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    SD_DMA_WritePendingState = SD_TRANSFER_OK;
}

#endif // DEVICE_SDIO
