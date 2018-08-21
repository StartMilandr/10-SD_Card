#include <stdio.h>

#include "brdClock.h"
#include "brdLed.h"
#include "brdBtn.h"
#include "brdUtils.h"
#include "brdSDIO.h"
#include "brdSD_Card.h"
#include "brdSysTimer.h"

//  FileSystem
#include "test_funcIO.h"

//  Свеодиоды для индикации статуса
#define LED_OK       BRD_LED_1
#define LED_FAULT    BRD_LED_2
#define LED_CYCLE    BRD_LED_3

#define LED_SEL_ALL   BRD_LED_1 | BRD_LED_2 | BRD_LED_3

//  Настройки тактирования.
//  Системынй таймер с периодом 1мс используется для отработки таймаутов при работе с картой.
//  Таймер запускается в brdSDIO.c в функции BRD_SDIO_Init(), 
//  которая вызывается при инициализации SD карты - BRD_SD_Init()
#define PLL_MULL      RST_CLK_CPU_PLLmul10
#define PERIOD_1MS    (HSE_Value * (PLL_MULL + 1) / 1000)

uint32_t TmrDelay_ms = 1000;

//  Тесты в цикле - Активным может быть только один
void While_TestFS(void);
void While_SDIO_TestCMD(void);
void While_SDIO_TestCMD_8Clk(void);
void While_SDIO_TestCycles(void);
void While_SDIO_TestRate(void);

//  Таумауты для функций блока SDIO
BRD_SDDrv_Timeouts sdioTimeouts =
{
  .SD_SendCMD_Timout_ms     = 100,
  .SD_BeforeRead_Delay_ms   = 2,
  .SD_ReadResp_Timout_ms    = 100,
  .SD_ChangeRate_Timout_ms  = 2,
  .SD_ReadBlock_Timout_ms   = 100,
  .SD_WriteBlock_Timout_ms  = 1000  
} ;

//  Таумауты и настройки для работы с SD картой
BRD_SD_ClockCfg sdioCardCfg =
{
  .SysTimerPeriod_1ms    = PERIOD_1MS,       // Value to get 1ms timer period  
  .SD_CLK_BRG_Detect     = SDIO_BRG_div256,  // 80MHz/256 = 312,5 KHz < 400KHz
  .SD_CLK_BRG_Transfer   = SDIO_BRG_div16,   // 80MHz/16  = 5 MHz - Работает всегда
    //.SD_CLK_BRG_Transfer   = SDIO_BRG_div8,  // 80MHz/8  = 10MHz - Сбой каждый 9-й запуск
    //.SD_CLK_BRG_Transfer   = SDIO_BRG_div4,  // 80MHz/2  = 20MHz - Идентификация работает, тест файловой системы НЕТ!
                          
  .SD_PowerOn_Delay_ms     = 2,              //     2 ms
  .SD_WaitStatus_Timout_ms = 500,            //   500 ms  
  .SD_Erase_Timout_ms      = 30000           // 30000 ms   
} ;


int main(void)
{ 
  //  Clock 80MHz
  BRD_Clock_Init_HSE_PLL(RST_CLK_CPU_PLLmul10);
	
  //  Leds and buttons Init
  BRD_LEDs_Init();
  BRD_BTNs_Init();
  
  //  Инициализации SD карты 
  //    Инициализация SDIO или SPI запускается внутри, выбор - по макроопределению в brdSD_Card.h
  BRD_SD_Init(&sdioCardCfg, &sdioTimeouts);
  
  //  Запуск системного таймера - для отработки таймаутов.
  SysTickStart(PERIOD_1MS);
  
// Тестовые процедуры с бесконечным циклом внутри
  //    Test идентификации карты и файловой системы
  While_TestFS();
  
  //    Вспомогательные тесты для SDIO
  //  While_SDIO_TestCMD();
  //  While_SDIO_TestCMD_8Clk();
  //  While_SDIO_TestCycles();
  //  While_SDIO_TestRate();
}

void While_TestFS(void)
{
  volatile BRD_SD_Detect_Result CardDetectResult;  
 
  while (1)
  {
    // LED_CYCLE показывает что МК работает - мигает раз в секунду
    if (!TmrDelay_ms)
    {
      BRD_LED_Switch(LED_CYCLE);
      TmrDelay_ms = 1000;
    }

    //  Тест файловой системы
    if (BRD_Is_BntAct_Down())
    {  
      while (BRD_Is_BntAct_Down());
      
      BRD_LED_Set(LED_SEL_ALL, 0);
      
      if (TestFileSystem() == 0)
        BRD_LED_Set(LED_OK, 1);
      else
        BRD_LED_Set(LED_FAULT, 1);
    }    

    //  Тест идентификации карты
    if (BRD_Is_BntAct_Select())
    {  
      while (BRD_Is_BntAct_Select());
      
      BRD_LED_Set(LED_SEL_ALL, 0);
      
      CardDetectResult = BRD_SD_CardDetect();
      if (CardDetectResult == SD_OK)
      {
        printf("SDIO Card Detected!\n");
        BRD_LED_Set(LED_OK, 1);
      }
      else
      {  
        printf("SDIO Card Detection FAILED!\n");
        BRD_LED_Set(LED_FAULT, 1);
      }
    }
  }  
}

// Обработчик системного таймера
void SysTick_Handler(void)
{
  if (TmrDelay_ms)
    --TmrDelay_ms;
  
  BRD_SDIO_DelayHandler();
}

// Для проверки осциллографом где какие данные в посылке
void While_SDIO_TestCMD(void)
{
  BRD_SDIO_PowerOn();
  BRD_SDIO_SetClockBRG(SDIO_BRG_div32);

  while (1)
  {
    BRD_SDIO_SendCMD(CMD0, 0);
    BRD_SDIO_SendCMD(CMD0, 0x000000FF);
    BRD_SDIO_SendCMD(CMD0, 0xFF000000);  
    BRD_SDIO_SendCMD(0x3F, 0); 
  }
}

// Для проверки осциллографом, что к команде без ответа добавляются 8 тактов частоты
//   Для завершения операции картой
void While_SDIO_TestCMD_8Clk(void)
{
  BRD_SDIO_PowerOn();
  BRD_SDIO_SetClockBRG(SDIO_BRG_div32);

  while (1)
  {  
    BRD_SDIO_ExecCMD(CMD0, 0, SD_Resp_No, NULL);
  }
}

// Для проверки осциллографом, как формируются пусты такты частоты (линия CMD = 1)
void While_SDIO_TestCycles(void)
{
  static uint32_t CycleCount = 8;
  
  BRD_SDIO_PowerOn();
  BRD_SDIO_SetClockBRG(SDIO_BRG_div32);

  while (1)
  {  
    BRD_SDIO_Send_ClockCycles(CycleCount);
    
    if (BRD_Is_BntAct_Left())
    { 
      while(BRD_Is_BntAct_Left());
      
      if (CycleCount > 0)      
        CycleCount -= 1;      
    }
    
    if (BRD_Is_BntAct_Right())
    {  
      while(BRD_Is_BntAct_Right());
      
      CycleCount += 1;
    }
  }
}

// Для проверки осциллографом, как выполняются функции на высокой частоте обмена
void While_SDIO_TestRate(void)
{
  volatile BRD_SD_Detect_Result CardDetectResult;    
  volatile bool startCyclic = false;
  
  //  Slow Rate
  CardDetectResult = BRD_SD_CardDetect();
  if (CardDetectResult == SD_OK)
  {
    printf("SDIO Card Detected!\n");
    BRD_LED_Set(LED_OK, 1);
  }
  else
  {  
    printf("SDIO Card Detection FAILED!\n");
    BRD_LED_Set(LED_FAULT, 1);
  }
  
  // High Rate
  while(1)
  {   
    if (BRD_Is_BntAct_Down())
    {  
      while(BRD_Is_BntAct_Down());
      startCyclic = !startCyclic;
    }
    
    if (startCyclic)
      SD_WaitFor_CardStatus(SD_State_Msk_Transf, sdioCardCfg.SD_WaitStatus_Timout_ms);
  }
}
