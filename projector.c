/*******************************************************
Chip type               : ATmega16
AVR Core Clock frequency: 8,000000 MHz
24.04.2017
*******************************************************/
#include <avr/io.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include "variables.h"
#include "rotation-counter.h"

/* Текущий статус идентификации ИК посылки.
  Может принимать значения:
  0 - Wait
  1 - SyncStart
  2 - SyncEnd
  3 - Data1
*/
int IR_Status = 0;

int PTime;          /* Значение счетчика в момент срабатывания прерывания */

int pre_1   = 0;    /* Значение счетчика для преамбулы */
int PRE1Min = 0x43; /* Минимальное значения счетчика преамбулы для протокола */
int PRE1Max = 0x53; /* Максимальное значения счетчика преамбулы для протокола */
int pre_2   = 0;    /* Значение счетчика для паузы после преамбулы */
int bit     = 0;    /* Флаг для определения пика/паузы в импульсах */
int bitCnt  = 0;    /* Счетчик для бит команды и адреса */

int IRData  = 0b0;  /* Два байта адреса */
int IRData2 = 0b0;  /* Два байта команды */

uint16_t EEMEM Adress       = 0xc;    /* Адрес устройства */
uint16_t EEMEM CommandDown  = 0xd827; /* Команда опускания экрана (SRS)*/
uint16_t EEMEM CommandUp    = 0x9966; /* Команда поднятия экрана (PIP)*/

void Start_TIM(void) {
    TIMSK = (0<<OCIE0) | (1<<TOIE0); /* TOIE0 - Разрешает переполнение */
    TCCR0 = (0<<WGM00) | (0<<COM01) | (0<<COM00) | (0<<WGM01) | (1<<CS02) | (0<<CS01) | (1<<CS00); /* предделитель 1024 */
    TCNT0 = 0x00;
}
void Stop_TIM(void) { /* Останавливаем и сбрасываем счетчик */
    TCCR0 = (0<<CS02) | (0<<CS01) | (0<<CS00);
    TCNT0 = 0x00;
}
void Reload_TIM(void) {
    TCNT0 = 0x00; /* Сбрасываем счетчик */
}

/* Сброс IR прерывания на статус 0 */
void Reset_IR(void){
    Stop_TIM();                          /* Останавливаем счетчик */
    IR_Status = 0;                       /* Статус 0 */
    bitCnt    = 0;                       /* Счетчик битов 0 */
    MCUCR     = (1<<ISC01) | (0<<ISC00); /* Сброс фронта срабатывания прерывания на спадающий ``\__ */
}

void up(void){

    int encCounter = eeprom_read_byte(&rotationCounter);

    if(encCounter == TOPLimit) {

        PORTB &= ~(1<<PINB3); /* Зажигаем индикацию */
        PORTB &= ~(1<<PINB1); /* Останавливаем двигатель если запущен */
        PORTB &= ~(1<<PINB0); /* Выставляем направление вращения */
        PORTB |=  (1<<PINB1); /* Стартуем двигатель */

        eeprom_write_byte(&rotationCounter, encCounter - 1); /* чтобы пройти условие whilа в countRotation() */
        eeprom_write_byte(&rotationDirection, 1); /* Записываем в EEPROM направление движения вала */
        eeprom_write_byte(&rotationDebt, 1);      /* Устанавливаем флаг rotationDebt */

        countRotation();

    } else {
        PORTB &= ~(1<<PINB3); /* Зажигаем индикацию */
        _delay_ms(200);
        PORTB |=  (1<<PINB3); /* Гасим индикацию */
    }
}

void down(void){

    int encCounter = eeprom_read_byte(&rotationCounter);

    if(encCounter == 0) {

        PORTB &= ~(1<<PINB3); /* Зажигаем индикацию */
        PORTB &= ~(1<<PINB1); /* Останавливаем двигатель если запущен */
        PORTB |=  (1<<PINB0); /* Выставляем направление вращения */
        PORTB |=  (1<<PINB1); /* Стартуем двигатель */

        eeprom_write_byte(&rotationCounter, encCounter + 1); /* чтобы пройти условие whilа в countRotation() */
        eeprom_write_byte(&rotationDirection, 0); /* Записываем в EEPROM направление движения вала */
        eeprom_write_byte(&rotationDebt, 1);      /* Устанавливаем флаг rotationDebt */

        countRotation();

    } else {
        PORTB &= ~(1<<PINB3); /* Зажигаем индикацию */
        _delay_ms(200);
        PORTB |=  (1<<PINB3); /* Гасим индикацию */
    }
}

ISR(INT0_vect) {
    GICR &=~ (1<<INT0);         /* Запрет срабатывания внешнего прерывания INT0 */
    PTime = TCNT0;              /* Сохраняем значение счетчика */
    MCUCR = MCUCR ^ (1<<ISC00); /* инвертирование фронта срабатывания прерывания */
    Reload_TIM();               /* Сбрасываем значение счетчика */

    switch (IR_Status) {
        case 0: {
            Start_TIM();        /* Запускаем счетчик */
            IR_Status = 1;      /* Следующий статус = 1 */
            break;
        }
        case 1: {
            pre_1 = PTime;      /* Сохраняем значение счетчика(ширина преамбулы) */
            IR_Status = 2;      /* Следующий статус = 2 */
            break;
        }
        case 2: {
            pre_2 = PTime;      /* Сохраняем значение счетчика(ширина паузы после преамбулы) */
            IR_Status = 3;      /* Следующий статус = 3 */
            break;
        }
        case 3: {
            Stop_TIM(); /* Останавливаем счетчик для проверок(чтобы не срабатывало прерывание по переполнению) */
            if ((PRE1Min < pre_1) && (pre_1 < PRE1Max)) { /* Проверяем ширину преамбулы для конкретного протокола */
                if (bit == 0) {
                    bit = 1;                         /* инвертируем если пик */
                } else {
                    bit = 0;                         /* инвертируем если пауза */
                    if (bitCnt < 16) {               /* считываем два байта адреса */
                        IRData = IRData << 1;        /* Сдвигаем все биты на один влево */
                        if (PTime > 7) {
                            IRData = IRData | 1<<0;  /* записываем в 0-й бит 1-цу если пауза больше 7 */
                        }
                    } else {
                        IRData2 = IRData2 << 1;      /* Сдвигаем все биты на один влево */
                        if (PTime > 7) {
                            IRData2 = IRData2 | 1<<0;/* записываем в 0-й бит 1-цу если пауза больше 7 */
                        }
                    }
                    bitCnt++;                        /* Увеличиваем счетчик считаных бит */
                }

                if (bitCnt > 31) {
                    Reset_IR();                      /* Увеличиваем счетчик считаных бит 0 */
                    uint16_t Addr;
                    Addr = eeprom_read_word(&Adress); /* Считываем адрес устройства */
                    if (IRData == Addr) {             /* Проверяем адрес */
                        /* Проверяем совпадение для ПЕРВОЙ команды */
                        if (IRData2 == eeprom_read_word(&CommandDown)) {
                            GICR &=~ (1<<INT0); /* Запрет срабатывания внешнего прерывания INT0 */
                            down(); /* Опускаем экран */
                            GIFR |= (1<<INTF1)|(1<<INTF0); /* сброс флагов прерывания пока ты в прерывании */
                            GICR |= (1<<INT0);  /* Разрешение срабатывания внешнего прерывания INT0 */
                        }
                        /* Проверяем совпадение для ВТОРОЙ команды */
                        if (IRData2 == eeprom_read_word(&CommandUp)) {
                            GICR &=~ (1<<INT0); /* Запрет срабатывания внешнего прерывания INT0 */
                            up();   /* Подымаем экран */
                            GIFR |= (1<<INTF1)|(1<<INTF0); /* сброс флагов прерывания пока ты в прерывании */
                            GICR |= (1<<INT0);  /* Разрешение срабатывания внешнего прерывания INT0 */
                        }
                    }
                } else {
                    Start_TIM();   /* Запускаем счетчик */
                }

            } else {
                Reset_IR();        /* Сбрасываем IR прерывание */
            }
            break;
        }
    }

    GIFR |= (1<<INTF1)|(1<<INTF0); /* сброс флагов прерывания пока ты в прерывании */
    GICR |= (1<<INT0);             /* Разрешение срабатывания внешнего прерывания INT0 */
    Reload_TIM();
}


/* обработка ошибки */
ISR(TIMER0_OVF_vect) {
    Reset_IR();             /* возвращаем обработку ИК посылки в начальное состояние */
}


int main(void) {

    DDRD = 0x00;

    PORTD = (1<<PIND2);

    PORTB = 0b00001000;
    DDRB  = 0b11111011;

    /* External Interruption configuration */
    MCUCR |= (1<<ISC01); /* configuration ```\_ (front) */
    GICR  |= (1<<INT0);

    sei();

    /* Проверка положения экрана при старте */
    int debt = eeprom_read_byte(&rotationDebt);
    if(debt != 0) {

        int direction = eeprom_read_byte(&rotationDirection);
        PORTB &= ~(1<<PINB3); /* Зажигаем индикацию */
        PORTB &= ~(1<<PINB1); /* Останавливаем двигатель если запущен */

        if(direction == 1) {
            PORTB &= ~(1<<PINB0); /* Выставляем направление вращения */
        } else {
            PORTB |=  (1<<PINB0); /* Выставляем направление вращения */
        }

        PORTB |=  (1<<PINB1); /* Стартуем двигатель */

        countRotation();
    }

    while(1){}

}
