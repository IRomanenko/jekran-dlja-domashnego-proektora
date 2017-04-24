
void countRotation(void) {

    int curentState = PINB & 0x04;

    int encCounter = eeprom_read_byte(&rotationCounter);

    int direction  = eeprom_read_byte(&rotationDirection);

    while((encCounter > 0) && (encCounter < TOPLimit)) {

        int newState = PINB & 0x04;

        if (curentState != newState) {

            if(direction == 1) {
                encCounter = encCounter - 1;
            } else {
                encCounter = encCounter + 1;
            }

            eeprom_write_byte(&rotationCounter, encCounter);

            curentState = newState;

        }
    }

    PORTB &= ~(1<<PINB1); /* Останавливаем двигатель */
    PORTB |=  (1<<PINB3); /* Гасим индикацию */

    eeprom_write_byte(&rotationDirection, 0); /* Записываем в EEPROM направление движения вала */
    eeprom_write_byte(&rotationDebt, 0);      /* Устанавливаем флаг rotationDebt в 0 */

}
