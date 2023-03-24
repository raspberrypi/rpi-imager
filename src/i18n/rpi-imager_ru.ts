<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="171"/>
        <source>Error writing to storage</source>
        <translation>Ошибка записи на запоминающее устройство</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>Ошибка извлечения архива: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Ошибка подключения раздела FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Операционная система не подключила раздел FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Ошибка перехода в каталог «%1»</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error running diskpart: %1</source>
        <translation>Ошибка выполнения diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="168"/>
        <source>Error removing existing partitions</source>
        <translation>Ошибка удаления существующих разделов</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="194"/>
        <source>Authentication cancelled</source>
        <translation>Аутентификация отменена</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="197"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Ошибка выполнения authopen для получения доступа к дисковому устройству «%1»</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="198"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Убедитесь, что в параметрах настройки конфиденциальности (privacy settings) в разделе «файлы и папки» («files and folders») программе Raspberry Pi Imager разрешён доступ к съёмным томам (removable volumes). Либо можно предоставить программе доступ ко всему диску (full disk access).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="220"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Не удалось открыть запоминающее устройство «%1».</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="262"/>
        <source>discarding existing data on drive</source>
        <translation>удаление существующих данных на диске</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>обнуление первого и последнего мегабайта диска</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="288"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Ошибка записи при обнулении MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="779"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Ошибка чтения с запоминающего устройства.&lt;br&gt;SD-карта может быть повреждена.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="855"/>
        <source>Waiting for FAT partition to be mounted</source>
        <translation>Ожидание подключения раздела FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="941"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Ошибка подключения раздела FAT32</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="963"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Операционная система не подключила раздел FAT32</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="996"/>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation>Не удалось настроить. Файл «%1» не существует.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1000"/>
        <source>Customizing image</source>
        <translation>Настройка образа</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1085"/>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation>Ошибка создания firstrun.sh в разделе FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1037"/>
        <source>Error writing to config.txt on FAT partition</source>
        <translation>Ошибка записи в config.txt в разделе FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1102"/>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation>Ошибка создания файла user-data cloudinit в разделе FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1116"/>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation>Ошибка создания файла network-config cloudinit в разделе FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1139"/>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation>Ошибка записи в cmdline.txt в разделе FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="432"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Ошибка доступа при записи файла на диск.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="437"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Похоже, что включён контролируемый доступ к папкам (Controlled Folder Access). Добавьте rpi-imager.exe и fat32format.exe в список разрешённых программ и попробуйте снова.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="444"/>
        <source>Error writing file to disk</source>
        <translation>Ошибка записи файла на диск</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="463"/>
        <source>Error downloading: %1</source>
        <translation>Ошибка загрузки: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Ошибка записи на запоминающее устройство (при сбросе)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="693"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Ошибка записи на запоминающее устройство (при выполнении fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="674"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Загруженные данные повреждены. Хэш не совпадает</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="114"/>
        <source>opening drive</source>
        <translation>открытие диска</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="300"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Ошибка записи при попытке обнулить последнюю часть карты.&lt;br&gt;Заявленный картой объём может не соответствовать действительности (возможно, карта является поддельной).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="389"/>
        <source>starting download</source>
        <translation>начало загрузки</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <source>Error writing first block (partition table)</source>
        <translation>Ошибка записи первого блока (таблица разделов)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="798"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Ошибка по результатам проверки записи. Содержимое SD-карты отличается от того, что было на неё записано.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Ошибка создания разделов: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Ошибка запуска fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Ошибка выполнения fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Ошибка определения новой буквы диска</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Недопустимое устройство: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Ошибка форматирования (с помощью udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Ошибка запуска sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>При создании разделов не был создан ожидаемый раздел FAT %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Ошибка запуска mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Ошибка выполнения mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Для этой платформы не реализовано форматирование</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="257"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Недостаточная ёмкость запоминающего устройства.&lt;br&gt;Требуется не менее %1 ГБ.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="263"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Входной файл не представляет собой корректный образ диска.&lt;br&gt;Размер файла %1 не кратен 512 байтам.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="445"/>
        <source>Downloading and writing image</source>
        <translation>Загрузка и запись образа</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="578"/>
        <source>Select image</source>
        <translation>Выбор образа</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="979"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Указать пароль от WiFi автоматически (взяв его из системной цепочки ключей)?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>открытие файла образа</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>Ошибка открытия файла образа</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>НЕТ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>ДА</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
        <source>CONTINUE</source>
        <translation>ПРОДОЛЖИТЬ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="130"/>
        <source>QUIT</source>
        <translation>ВЫЙТИ</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="79"/>
        <source>Advanced options</source>
        <translation>Дополнительные параметры</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="96"/>
        <source>Image customization options</source>
        <translation>Параметры настройки образа</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="104"/>
        <source>for this session only</source>
        <translation>только для этого сеанса</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="105"/>
        <source>to always use</source>
        <translation>для постоянного использования</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="119"/>
        <source>Set hostname:</source>
        <translation>Имя хоста:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="138"/>
        <source>Enable SSH</source>
        <translation>Включить SSH</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="160"/>
        <source>Use password authentication</source>
        <translation>Использовать аутентификацию по паролю</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="170"/>
        <source>Allow public-key authentication only</source>
        <translation>Разрешить только аутентификацию с использованием открытого ключа</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="188"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>authorized_keys для «%1»:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="261"/>
        <source>Configure wireless LAN</source>
        <translation>Настроить Wi-Fi</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="280"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="234"/>
        <location filename="../OptionsPopup.qml" line="300"/>
        <source>Password:</source>
        <translation>Пароль:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="200"/>
        <source>Set username and password</source>
        <translation>Указать имя пользователя и пароль</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="219"/>
        <source>Username:</source>
        <translation>Имя пользователя:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="295"/>
        <source>Hidden SSID</source>
        <translation>Скрытый идентификатор SSID</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="316"/>
        <source>Show password</source>
        <translation>Показывать пароль</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="321"/>
        <source>Wireless LAN country:</source>
        <translation>Страна Wi-Fi:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="332"/>
        <source>Set locale settings</source>
        <translation>Указать параметры региона</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="342"/>
        <source>Time zone:</source>
        <translation>Часовой пояс:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="352"/>
        <source>Keyboard layout:</source>
        <translation>Раскладка клавиатуры:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="365"/>
        <source>Persistent settings</source>
        <translation>Постоянные параметры</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="373"/>
        <source>Play sound when finished</source>
        <translation>Воспроизводить звук после завершения</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="377"/>
        <source>Eject media when finished</source>
        <translation>Извлекать носитель после завершения</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="381"/>
        <source>Enable telemetry</source>
        <translation>Включить телеметрию</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="394"/>
        <source>SAVE</source>
        <translation>СОХРАНИТЬ</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>Внутреннее устройство чтения SD-карт</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="72"/>
        <source>Warning: advanced settings set</source>
        <translation>Внимание: установлены дополнительные параметры</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="86"/>
        <source>Would you like to apply the image customization settings saved earlier?</source>
        <translation>Применить сохранённые ранее параметры настройки образа?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="95"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>НЕТ, ОЧИСТИТЬ ПАРАМЕТРЫ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="105"/>
        <source>YES</source>
        <translation>ДА</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>EDIT SETTINGS</source>
        <translation>ИЗМЕНИТЬ ПАРАМЕТРЫ</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager, версия %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="99"/>
        <location filename="../main.qml" line="399"/>
        <source>Operating System</source>
        <translation>Операционная система</translation>
    </message>
    <message>
        <location filename="../main.qml" line="111"/>
        <source>CHOOSE OS</source>
        <translation>ВЫБРАТЬ ОС</translation>
    </message>
    <message>
        <location filename="../main.qml" line="123"/>
        <source>Select this button to change the operating system</source>
        <translation>Нажмите эту кнопку, чтобы изменить операционную систему</translation>
    </message>
    <message>
        <location filename="../main.qml" line="135"/>
        <location filename="../main.qml" line="713"/>
        <source>Storage</source>
        <translation>Запоминающее устройство</translation>
    </message>
    <message>
        <location filename="../main.qml" line="147"/>
        <location filename="../main.qml" line="1038"/>
        <source>CHOOSE STORAGE</source>
        <translation>ВЫБРАТЬ ЗАПОМИНАЮЩЕЕ УСТРОЙСТВО</translation>
    </message>
    <message>
        <location filename="../main.qml" line="173"/>
        <source>WRITE</source>
        <translation>ЗАПИСАТЬ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="177"/>
        <source>Select this button to start writing the image</source>
        <translation>Нажмите эту кнопку, чтобы начать запись образа</translation>
    </message>
    <message>
        <location filename="../main.qml" line="218"/>
        <source>CANCEL WRITE</source>
        <translation>ОТМЕНИТЬ ЗАПИСЬ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <location filename="../main.qml" line="965"/>
        <source>Cancelling...</source>
        <translation>Отмена...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="229"/>
        <source>CANCEL VERIFY</source>
        <translation>ОТМЕНИТЬ ПРОВЕРКУ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="232"/>
        <location filename="../main.qml" line="988"/>
        <location filename="../main.qml" line="1057"/>
        <source>Finalizing...</source>
        <translation>Завершение...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="247"/>
        <source>Select this button to access advanced settings</source>
        <translation>Нажмите эту кнопку для получения доступа к дополнительным параметрам</translation>
    </message>
    <message>
        <location filename="../main.qml" line="261"/>
        <source>Using custom repository: %1</source>
        <translation>Использование настраиваемого репозитория: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="270"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Навигация с помощью клавиатуры: клавиша &lt;Tab&gt; перемещает на следующую кнопку, клавиша &lt;Пробел&gt; нажимает кнопку/выбирает элемент, клавиши со &lt;стрелками вверх/вниз&gt; перемещают выше/ниже в списке</translation>
    </message>
    <message>
        <location filename="../main.qml" line="290"/>
        <source>Language: </source>
        <translation>Язык: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="313"/>
        <source>Keyboard: </source>
        <translation>Клавиатура: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="505"/>
        <location filename="../main.qml" line="1022"/>
        <source>Erase</source>
        <translation>Стереть</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Format card as FAT32</source>
        <translation>Отформатировать карту как FAT32</translation>
    </message>
    <message>
        <location filename="../main.qml" line="515"/>
        <source>Use custom</source>
        <translation>Использовать настраиваемый образ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="516"/>
        <source>Select a custom .img from your computer</source>
        <translation>Выбрать настраиваемый файл .img на компьютере</translation>
    </message>
    <message>
        <location filename="../main.qml" line="461"/>
        <source>Back</source>
        <translation>Назад</translation>
    </message>
    <message>
        <location filename="../main.qml" line="157"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Нажмите эту кнопку, чтобы изменить целевое запоминающее устройство</translation>
    </message>
    <message>
        <location filename="../main.qml" line="462"/>
        <source>Go back to main menu</source>
        <translation>Вернуться в главное меню</translation>
    </message>
    <message>
        <location filename="../main.qml" line="628"/>
        <source>Released: %1</source>
        <translation>Дата выпуска: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="638"/>
        <source>Cached on your computer</source>
        <translation>Кэш на компьютере</translation>
    </message>
    <message>
        <location filename="../main.qml" line="640"/>
        <source>Local file</source>
        <translation>Локальный файл</translation>
    </message>
    <message>
        <location filename="../main.qml" line="641"/>
        <source>Online - %1 GB download</source>
        <translation>Онлайн — объём загрузки составляет %1 ГБ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <location filename="../main.qml" line="818"/>
        <location filename="../main.qml" line="824"/>
        <source>Mounted as %1</source>
        <translation>Подключено как %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="820"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[ЗАЩИЩЕНО ОТ ЗАПИСИ]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="862"/>
        <source>Are you sure you want to quit?</source>
        <translation>Действительно выполнить выход?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Программа Raspberry Pi Imager всё ещё занята.&lt;br&gt;Действительно выполнить выход из неё?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="874"/>
        <source>Warning</source>
        <translation>Внимание</translation>
    </message>
    <message>
        <location filename="../main.qml" line="882"/>
        <source>Preparing to write...</source>
        <translation>Подготовка к записи…</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Update available</source>
        <translation>Доступно обновление</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Доступна новая версия Imager.&lt;br&gt;Перейти на веб-сайт для её загрузки?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="968"/>
        <source>Writing... %1%</source>
        <translation>Запись... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="895"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Все существующие на «%1» данные будут стёрты.&lt;br&gt;Продолжить?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="947"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Ошибка загрузки списка ОС из Интернета</translation>
    </message>
    <message>
        <location filename="../main.qml" line="991"/>
        <source>Verifying... %1%</source>
        <translation>Проверка... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="998"/>
        <source>Preparing to write... (%1)</source>
        <translation>Подготовка к записи... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1014"/>
        <source>Error</source>
        <translation>Ошибка</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1021"/>
        <source>Write Successful</source>
        <translation>Запись успешно выполнена</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1023"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Данные на &lt;b&gt;%1&lt;/b&gt; стёрты&lt;br&gt;&lt;br&gt;Теперь можно извлечь SD-карту из устройства чтения</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1030"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Запись &lt;b&gt;%1&lt;/b&gt; на &lt;b&gt;%2&lt;/b&gt; выполнена&lt;br&gt;&lt;br&gt;Теперь можно извлечь SD-карту из устройства чтения</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1098"/>
        <source>Error parsing os_list.json</source>
        <translation>Ошибка синтаксического анализа os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1271"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Сначала подключите USB-накопитель с образами.&lt;br&gt;Образы должны находиться в корневой папке USB-накопителя.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1287"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SD-карта защищена от записи.&lt;br&gt;Переместите вверх блокировочный переключатель, расположенный с левой стороны карты, и попробуйте ещё раз.</translation>
    </message>
</context>
</TS>
