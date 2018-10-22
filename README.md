# Термометр Журавли (grus)

Используются библиотеки `OneWire`, `DallasTemperature` и `A6lib`.

В библиотеку `A6lib` внесены две корректировки.

1. В функции A6lib::begin закомментирована команда "AT+CPMS=ME,ME,ME", поскольку она не работает:
```
    // Set SMS storage to the GSM modem. If this doesn't work for you, 
    // try changing the command to: "AT+CPMS=SM,SM,SM"
//    if (A6_OK != A6command("AT+CPMS=ME,ME,ME", "OK", "yy", A6_CMD_TIMEOUT, 2, NULL))
        // This may sometimes fail, in which case the modem needs to be rebooted.
//    {
//        return A6_FAILURE;
//    }
```

2. В функции A6lib::detectRate расширен набор возможных скоростей:
```
//    unsigned long rates[] = {9600, 115200};
    unsigned long rates[] = { 28800, 115200, 57600, 38400, 19200, 9600, 2400, 4800, 14400, 28800};
```
