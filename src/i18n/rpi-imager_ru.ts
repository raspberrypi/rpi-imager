<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Ошибка извлечения архива: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Ошибка подключения раздела FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Операционная система не подключила раздел FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Ошибка перехода в каталог «%1»</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Ошибка чтения с запоминающего устройства.&lt;br&gt;SD-карта может быть повреждена.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Ошибка по результатам проверки записи. Содержимое SD-карты отличается от того, что было на неё записано.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>отключение диска</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>открытие диска</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Ошибка выполнения diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Ошибка удаления существующих разделов</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Аутентификация отменена</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Ошибка выполнения authopen для получения доступа к дисковому устройству «%1»</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Убедитесь, что в параметрах настройки конфиденциальности (privacy settings) в разделе «файлы и папки» («files and folders») программе Raspberry Pi Imager разрешён доступ к съёмным томам (removable volumes). Либо можно предоставить программе доступ ко всему диску (full disk access).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Не удалось открыть запоминающее устройство «%1».</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>удаление существующих данных на диске</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>обнуление первого и последнего мегабайта диска</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Ошибка записи при обнулении MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Ошибка записи при попытке обнулить последнюю часть карты.&lt;br&gt;Заявленный картой объём может не соответствовать действительности (возможно, карта является поддельной).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>начало загрузки</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Ошибка загрузки: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Ошибка доступа при записи файла на диск.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Похоже, что включён контролируемый доступ к папкам (Controlled Folder Access). Добавьте rpi-imager.exe и fat32format.exe в список разрешённых программ и попробуйте снова.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Ошибка записи файла на диск</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Ошибка записи на запоминающее устройство (при сбросе)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Ошибка записи на запоминающее устройство (при выполнении fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Ошибка записи первого блока (таблица разделов)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Ошибка чтения с запоминающего устройства.&lt;br&gt;SD-карта может быть повреждена.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Ошибка по результатам проверки записи. Содержимое SD-карты отличается от того, что было на неё записано.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Настройка образа</translation>
    </message>
    <message>
        <source>Cached file is corrupt. SHA256 hash does not match expected value.&lt;br&gt;The cache file will be removed and the download will restart.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Local file is corrupt or has incorrect SHA256 hash.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Download appears to be corrupt. SHA256 hash does not match.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2&lt;br&gt;Please check your network connection and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error partitioning: %1</source>
        <translation>Ошибка создания разделов: %1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>Ошибка запуска fat32format</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>Ошибка выполнения fat32format: %1</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>Ошибка определения новой буквы диска</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>Недопустимое устройство: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Ошибка форматирования (с помощью udisks2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Для этой платформы не реализовано форматирование</translation>
    </message>
    <message>
        <source>Error opening device for formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error writing to device during formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error seeking on device during formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Invalid parameters for formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Insufficient space on device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Unknown formatting error</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation type="unfinished">Запоминающее устройство</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="unfinished">Подключено как %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[ЗАЩИЩЕНО ОТ ЗАПИСИ]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD-карта защищена от записи.&lt;br&gt;Переместите вверх блокировочный переключатель, расположенный с левой стороны карты, и попробуйте ещё раз.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">ВЫБРАТЬ УСТРОЙСТВО</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Устройство Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Недостаточная ёмкость запоминающего устройства.&lt;br&gt;Требуется не менее %1 ГБ.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Входной файл не представляет собой корректный образ диска.&lt;br&gt;Размер файла %1 не кратен 512 байтам.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Загрузка и запись образа</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Выбор образа</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Указать пароль от WiFi автоматически (взяв его из системной цепочки ключей)?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>открытие файла образа</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Ошибка открытия файла образа</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <source>NO</source>
        <translation>НЕТ</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ДА</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>ПРОДОЛЖИТЬ</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>ВЫЙТИ</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation type="unfinished">Операционная система</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Назад</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Вернуться в главное меню</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Дата выпуска: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">Кэш на компьютере</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Локальный файл</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Онлайн — объём загрузки составляет %1 ГБ</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Сначала подключите USB-накопитель с образами.&lt;br&gt;Образы должны находиться в корневой папке USB-накопителя.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Имя хоста:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Указать имя пользователя и пароль</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Имя пользователя:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Пароль:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Настроить Wi-Fi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">Скрытый идентификатор SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Страна Wi-Fi:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Указать параметры региона</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Часовой пояс:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Раскладка клавиатуры:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Воспроизводить звук после завершения</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Извлекать носитель после завершения</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Включить телеметрию</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>General</source>
        <translation>Общие</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Службы</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Параметры</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>СОХРАНИТЬ</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">Включить SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Использовать аутентификацию по паролю</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Разрешить только аутентификацию с использованием открытого ключа</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">authorized_keys для «%1»:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">ВЫПОЛНИТЬ SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>NO</source>
        <translation>НЕТ</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>НЕТ, ОЧИСТИТЬ ПАРАМЕТРЫ</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ДА</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>ИЗМЕНИТЬ ПАРАМЕТРЫ</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager, версия %1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Устройство Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Нажмите эту кнопку, чтобы выбрать целевое устройство Raspberry Pi</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Операционная система</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>ВЫБРАТЬ ОС</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Нажмите эту кнопку, чтобы изменить операционную систему</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Запоминающее устройство</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>ВЫБРАТЬ ЗАПОМ. УСТРОЙСТВО</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Нажмите эту кнопку, чтобы изменить целевое запоминающее устройство</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>ОТМЕНИТЬ ЗАПИСЬ</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Отмена...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>ОТМЕНИТЬ ПРОВЕРКУ</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Завершение...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Далее</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Нажмите эту кнопку, чтобы начать запись образа</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Использование настраиваемого репозитория: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Навигация с помощью клавиатуры: клавиша &lt;Tab&gt; перемещает на следующую кнопку, клавиша &lt;Пробел&gt; нажимает кнопку/выбирает элемент, клавиши со &lt;стрелками вверх/вниз&gt; перемещают выше/ниже в списке</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Язык: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Клавиатура: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Действительно выполнить выход?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Программа Raspberry Pi Imager всё ещё занята.&lt;br&gt;Действительно выполнить выход из неё?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Внимание</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Подготовка к записи…</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Все существующие на «%1» данные будут стёрты.&lt;br&gt;Продолжить?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Доступно обновление</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Доступна новая версия Imager.&lt;br&gt;Перейти на веб-сайт для её загрузки?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Запись... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Проверка... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Подготовка к записи... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Ошибка</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Запись успешно выполнена</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Стереть</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Данные на &lt;b&gt;%1&lt;/b&gt; стёрты&lt;br&gt;&lt;br&gt;Теперь можно извлечь SD-карту из устройства чтения</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Запись &lt;b&gt;%1&lt;/b&gt; на &lt;b&gt;%2&lt;/b&gt; выполнена&lt;br&gt;&lt;br&gt;Теперь можно извлечь SD-карту из устройства чтения</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Отформатировать карту как FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Использовать настраиваемый образ</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Выбрать настраиваемый файл .img на компьютере</translation>
    </message>
    <message>
        <source>SKIP CACHE VERIFICATION</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Starting download...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Verifying cached file... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Verifying cached file...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Starting write...</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
