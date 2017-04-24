uint8_t  EEMEM rotationCounter   = 0;   /* Счетчик оборотов вала */
uint8_t  EEMEM rotationDirection = 0;   /* Флаг направления вращения */
uint8_t  EEMEM rotationDebt      = 0;   /* Флаг незаконченной операции поднятия/опускания экрана */

int TOPLimit = 36;                      /* Лимит срабатываний датчика Холла. Подбирается экспериментально */
