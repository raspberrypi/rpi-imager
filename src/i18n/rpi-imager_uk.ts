<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="uk_UA">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>Помилка розпакування архіва: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Помилка монтування FAT32 розділу</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Операційна система не монтувала FAT32 розділ</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Помилка при зміні каталогу на &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error writing to storage</source>
        <translation type="vanished">Помилка запису на накопичувач</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="119"/>
        <source>unmounting drive</source>
        <translation>диск від&apos;єднується</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="139"/>
        <source>opening drive</source>
        <translation>диск відкривається</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="167"/>
        <source>Error running diskpart: %1</source>
        <translation>Помилка при виконанні diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="188"/>
        <source>Error removing existing partitions</source>
        <translation>Помилка при видаленні існуючих розділів</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="214"/>
        <source>Authentication cancelled</source>
        <translation>Аутентифікація скасована</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Помилка виконання authopen для отримання доступу до пристроя &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="218"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Переконайтеся, що у Raspberry Pi Imager у налаштуваннях приватності (у розділі &quot;файли та каталоги&quot;) є доступ до змінних розділів. Або дайте програмі доступ до усього диску.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="240"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Не вдалося відкрити накопичувач &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>discarding existing data on drive</source>
        <translation>видалення існуючих даних на диску</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="302"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>обнулювання першого і останнього мегабайта диску</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="308"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Помилка при обнулюванні MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="320"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Помилка запису під час обнулювання останнього розділу карти пам&apos;яті.&lt;br&gt;Можливо заявлений об&apos;єм карти не збігається з реальним (можливо карта є підробленою).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="409"/>
        <source>starting download</source>
        <translation>початок завантаження</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="469"/>
        <source>Error downloading: %1</source>
        <translation>Помилка завантаження: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="666"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Помилка доступу при записі файлу на диск.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="671"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Схоже, що увімкнено контрольований доступ до каталогу (Controlled Folder Access). Додайте rpi-imager.exe і fat32format.exe в список виключення та спробуйте ще раз.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="678"/>
        <source>Error writing file to disk</source>
        <translation>Помилка запису файлу на диск</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="700"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Завантаження пошкоджено. Хеш сума не збігається</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="712"/>
        <location filename="../downloadthread.cpp" line="764"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Помилка запису на накопичувач (при скидуванні)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <location filename="../downloadthread.cpp" line="771"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Помилка запису на накопичувач (при виконанні fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="754"/>
        <source>Error writing first block (partition table)</source>
        <translation>Помилка під час запису першого блоку (таблиця розділів)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="829"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Помилка читання накопичувача.&lt;br&gt;SD-карта пам&apos;яті може бути пошкоджена.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="848"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Помилка перевірки запису. Зміст SD-карти пам&apos;яті відрізняється від того, що було записано туди.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="901"/>
        <source>Customizing image</source>
        <translation>Налаштування образа</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Помилка створення роздіу: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Помилка запуску fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Помилка під час виконання fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Помилка визначення  нової букви диску</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Не правильний пристрій: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Помилка форматування (через udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Помилка запуску sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>При створенні розділів не було створено очікуваний розділ FAT %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Помилка запуску mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Помилка виконання mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Форматування не доступно на цій платформі</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <location filename="../DstPopup.qml" line="24"/>
        <source>Storage</source>
        <translation type="unfinished">Накопичувач</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="38"/>
        <source>No storage devices found</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="67"/>
        <source>Exclude System Drives</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="94"/>
        <source>gigabytes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="96"/>
        <location filename="../DstPopup.qml" line="152"/>
        <source>Mounted as %1</source>
        <translation type="unfinished">Примонтовано як %1</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="139"/>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="154"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">ЗАХИЩЕНО ВІД ЗАПИСУ</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="156"/>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="197"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD карта захищена від запису.&lt;br&gt;перетягніть перемикач на лівій стороні картки вгору, і спробуйте ще раз.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <location filename="../hwlistmodel.cpp" line="111"/>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">ОБРАТИ ПРИСТРІЙ</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <location filename="../HwPopup.qml" line="27"/>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Пристрій Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="301"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Місця на накопичувачі недостатньо.&lt;br&gt;Треба, щоб було хоча б %1 ГБ.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="307"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Обраний файл не є правильним образом диску.&lt;br&gt;Розмір файла %1 байт не є кратним 512 байт.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="716"/>
        <source>Downloading and writing image</source>
        <translation>Завантаження і запис образу</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="814"/>
        <source>Select image</source>
        <translation>Обрати образ</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="989"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1001"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1212"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Вказати пароль від Wi-Fi автоматично із системного ланцюга ключів?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="36"/>
        <source>opening image file</source>
        <translation>відкривання файлу образа</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="41"/>
        <source>Error opening image file</source>
        <translation>Помилка при відкриванні файлу образу</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="52"/>
        <source>NO</source>
        <translation>НІ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="61"/>
        <source>YES</source>
        <translation>ТАК</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="70"/>
        <source>CONTINUE</source>
        <translation>ПРОДОВЖИТИ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="78"/>
        <source>QUIT</source>
        <translation>ВИЙТИ</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <location filename="../oslistmodel.cpp" line="211"/>
        <source>Recommended</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <location filename="../OSPopup.qml" line="30"/>
        <source>Operating System</source>
        <translation type="unfinished">Операційна система</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="92"/>
        <source>Back</source>
        <translation type="unfinished">Назад</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="93"/>
        <source>Go back to main menu</source>
        <translation type="unfinished">Повернутися у головне меню</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="250"/>
        <source>Released: %1</source>
        <translation type="unfinished">Випущено: %1</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="260"/>
        <source>Cached on your computer</source>
        <translation type="unfinished">Кешовано на вашому комп&apos;ютері</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="262"/>
        <source>Local file</source>
        <translation type="unfinished">Локальний файл</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="263"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Онлайн - потрібно завантажити %1 ГБ</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="349"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Спочатку під&apos;єднайте USB-накопичувач з образами.&lt;br&gt;Образи повинні знаходитися у корінному каталогу USB-накопичувача.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="40"/>
        <source>Set hostname:</source>
        <translation type="unfinished">Встановити ім&apos;я хосту:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="68"/>
        <source>Set username and password</source>
        <translation type="unfinished">Встановити ім&apos;я користувача і пароль</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="81"/>
        <source>Username:</source>
        <translation type="unfinished">Ім&apos;я користувача:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="106"/>
        <location filename="../OptionsGeneralTab.qml" line="183"/>
        <source>Password:</source>
        <translation type="unfinished">Пароль:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="147"/>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Налаштувати бездротову LAN мережу</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="161"/>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="212"/>
        <source>Show password</source>
        <translation type="unfinished">Показати пароль</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="223"/>
        <source>Hidden SSID</source>
        <translation type="unfinished">Прихована SSID</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="234"/>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Країна бездротової LAN мережі:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="252"/>
        <source>Set locale settings</source>
        <translation type="unfinished">Змінити налаштування регіону</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="256"/>
        <source>Time zone:</source>
        <translation type="unfinished">Часова зона:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="275"/>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Розкладка клавіатури:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <location filename="../OptionsMiscTab.qml" line="23"/>
        <source>Play sound when finished</source>
        <translation type="unfinished">Відтворити звук після завершення</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="27"/>
        <source>Eject media when finished</source>
        <translation type="unfinished">Витягнути накопичувач після завершення</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="31"/>
        <source>Enable telemetry</source>
        <translation type="unfinished">Увімкнути телеметрію</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="28"/>
        <source>OS Customization</source>
        <translation>Налаштування ОС</translation>
    </message>
    <message>
        <source>OS customization options</source>
        <translation type="vanished">Опції налаштування ОС</translation>
    </message>
    <message>
        <source>for this session only</source>
        <translation type="vanished">тільки для цієї сесії</translation>
    </message>
    <message>
        <source>to always use</source>
        <translation type="vanished">для постійного використання</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="65"/>
        <source>General</source>
        <translation>Загальні</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="74"/>
        <source>Services</source>
        <translation>Сервіси</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="80"/>
        <source>Options</source>
        <translation>Налаштування</translation>
    </message>
    <message>
        <source>Set hostname:</source>
        <translation type="vanished">Встановити ім&apos;я хосту:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="vanished">Встановити ім&apos;я користувача і пароль</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="vanished">Ім&apos;я користувача:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="vanished">Пароль:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="vanished">Налаштувати бездротову LAN мережу</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="vanished">SSID:</translation>
    </message>
    <message>
        <source>Show password</source>
        <translation type="vanished">Показати пароль</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="vanished">Прихована SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="vanished">Країна бездротової LAN мережі:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="vanished">Змінити налаштування регіону</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="vanished">Часова зона:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="vanished">Розкладка клавіатури:</translation>
    </message>
    <message>
        <source>Enable SSH</source>
        <translation type="vanished">Увімкнути SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="vanished">Використовувати аутентефікацію через пароль</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="vanished">Дозволити аутентифікацію лише через публічні ключі</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="vanished">Встановити authorized_keys для &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="vanished">ЗАПУСТИТИ SHH-KEYGEN</translation>
    </message>
    <message>
        <source>Play sound when finished</source>
        <translation type="vanished">Відтворити звук після завершення</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="vanished">Витягнути накопичувач після завершення</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="vanished">Увімкнути телеметрію</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="123"/>
        <source>SAVE</source>
        <translation>ЗБЕРЕГТИ</translation>
    </message>
    <message>
        <source>Persistent settings</source>
        <translation type="vanished">Постійні налаштування</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <location filename="../OptionsServicesTab.qml" line="36"/>
        <source>Enable SSH</source>
        <translation type="unfinished">Увімкнути SSH</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="52"/>
        <source>Use password authentication</source>
        <translation type="unfinished">Використовувати аутентефікацію через пароль</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="63"/>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Дозволити аутентифікацію лише через публічні ключі</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="74"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Встановити authorized_keys для &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="121"/>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="140"/>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">ЗАПУСТИТИ SHH-KEYGEN</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="150"/>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="126"/>
        <source>Internal SD card reader</source>
        <translation>Внутрішній считувач SD карт</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Use OS customization?</source>
        <translation type="vanished">Використовувати налаштування ОС?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="41"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Чи бажаєте ви прийняти налаштування ОС?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="82"/>
        <source>NO</source>
        <translation>НІ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="63"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>НІ, ОЧИСТИТИ НАЛАШТУВАННЯ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="73"/>
        <source>YES</source>
        <translation>ТАК</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="50"/>
        <source>EDIT SETTINGS</source>
        <translation>РЕДАГУВАТИ НАЛАШТУВАННЯ</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="27"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager, версія %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="120"/>
        <source>Raspberry Pi Device</source>
        <translation>Пристрій Raspberry Pi</translation>
    </message>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="vanished">ОБРАТИ ПРИСТРІЙ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="144"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Оберіть цю кнопку, щоб обрати модель вашої Raspberry Pi</translation>
    </message>
    <message>
        <location filename="../main.qml" line="158"/>
        <source>Operating System</source>
        <translation>Операційна система</translation>
    </message>
    <message>
        <location filename="../main.qml" line="169"/>
        <location filename="../main.qml" line="446"/>
        <source>CHOOSE OS</source>
        <translation>ОБРАТИ ОС</translation>
    </message>
    <message>
        <location filename="../main.qml" line="182"/>
        <source>Select this button to change the operating system</source>
        <translation>Натисніть на цю кнопку, щоб змінити операційну систему</translation>
    </message>
    <message>
        <location filename="../main.qml" line="196"/>
        <source>Storage</source>
        <translation>Накопичувач</translation>
    </message>
    <message>
        <location filename="../main.qml" line="331"/>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="207"/>
        <location filename="../main.qml" line="668"/>
        <source>CHOOSE STORAGE</source>
        <translation>ОБРАТИ НАКОПИЧУВАЧ</translation>
    </message>
    <message>
        <source>WRITE</source>
        <translation type="vanished">ЗАПИСАТИ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Натисніть цю кнопку, щоб змінити пристрій призначення</translation>
    </message>
    <message>
        <location filename="../main.qml" line="266"/>
        <source>CANCEL WRITE</source>
        <translation>СКАСУВАТИ ЗАПИСУВАННЯ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="269"/>
        <location filename="../main.qml" line="591"/>
        <source>Cancelling...</source>
        <translation>Скасування...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="281"/>
        <source>CANCEL VERIFY</source>
        <translation>СКАСУВАТИ ПЕРЕВІРКУ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="284"/>
        <location filename="../main.qml" line="614"/>
        <location filename="../main.qml" line="687"/>
        <source>Finalizing...</source>
        <translation>Завершення...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="293"/>
        <source>Next</source>
        <translation>Далі</translation>
    </message>
    <message>
        <location filename="../main.qml" line="299"/>
        <source>Select this button to start writing the image</source>
        <translation>Натисніть цю кнопку, щоб розпочати запис образу</translation>
    </message>
    <message>
        <source>Select this button to access advanced settings</source>
        <translation type="vanished">Натисніть цю кнопку, щоб отримати доступ до розширених опцій</translation>
    </message>
    <message>
        <location filename="../main.qml" line="321"/>
        <source>Using custom repository: %1</source>
        <translation>Користуючись власним репозиторієм: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="340"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Навігація клавіатурою: клавіша &lt;Tab&gt; переміститися на наступну кнопку, клавіша &lt;Пробіл&gt; натиснути кнопку/обрати елемент, клавіши з &lt;стрілками вниз/вгору&gt; переміститися вниз/вгору по списку</translation>
    </message>
    <message>
        <location filename="../main.qml" line="361"/>
        <source>Language: </source>
        <translation>Мова: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="384"/>
        <source>Keyboard: </source>
        <translation>Клавіатура: </translation>
    </message>
    <message>
        <source>Pi model:</source>
        <translation type="vanished">Модель Raspberry Pi:</translation>
    </message>
    <message>
        <source>[ All ]</source>
        <translation type="vanished">[Усі]</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="vanished">Назад</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="vanished">Повернутися у головне меню</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="vanished">Випущено: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="vanished">Кешовано на вашому комп&apos;ютері</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="vanished">Локальний файл</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="vanished">Онлайн - потрібно завантажити %1 ГБ</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="vanished">Примонтовано як %1</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="vanished">ЗАХИЩЕНО ВІД ЗАПИСУ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="485"/>
        <source>Are you sure you want to quit?</source>
        <translation>Бажаєте вийти?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="486"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager все ще зайнятий.&lt;br&gt;Ви впевнені, що бажаєте вийти?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="497"/>
        <source>Warning</source>
        <translation>Увага</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Preparing to write...</source>
        <translation>Підготовка до запису...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="520"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Усі уснуючі дані у &apos;%1&apos; будуть видалені.&lt;br&gt; Ви впевнені, що бажаєте продовжити?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="535"/>
        <source>Update available</source>
        <translation>Доступно оновлення</translation>
    </message>
    <message>
        <location filename="../main.qml" line="536"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Доступна нова версія Imager.&lt;br&gt;Бажаєте завітати на сайт та завантажити її?</translation>
    </message>
    <message>
        <source>Error downloading OS list from Internet</source>
        <translation type="vanished">Помилка завантаження списку ОС із Інтернету</translation>
    </message>
    <message>
        <location filename="../main.qml" line="594"/>
        <source>Writing... %1%</source>
        <translation>Записування...%1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="617"/>
        <source>Verifying... %1%</source>
        <translation>Перевірка...%1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="624"/>
        <source>Preparing to write... (%1)</source>
        <translation>Підготовка до запису... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="644"/>
        <source>Error</source>
        <translation>Помилка</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Write Successful</source>
        <translation>Успішно записано</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <location filename="../imagewriter.cpp" line="648"/>
        <source>Erase</source>
        <translation>Видалити</translation>
    </message>
    <message>
        <location filename="../main.qml" line="653"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; був успішно видалено.br&gt;&lt;br&gt; тепер можна дістати SD карту із считувача</translation>
    </message>
    <message>
        <location filename="../main.qml" line="660"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Записування &lt;b&gt;%1&lt;/b&gt; на &lt;b&gt;%2&lt;/b&gt; виконано &lt;br&gt;&lt;br&gt; Тепер можна дістати SD карту із считувача</translation>
    </message>
    <message>
        <source>Error parsing os_list.json</source>
        <translation type="vanished">Помилка парсування os_list.json</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="649"/>
        <source>Format card as FAT32</source>
        <translation>Форматувати карту у FAT32</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="655"/>
        <source>Use custom</source>
        <translation>Власний образ</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="656"/>
        <source>Select a custom .img from your computer</source>
        <translation>Обрати власний .img з вашого комп&apos;ютера</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="vanished">Спочатку під&apos;єднайте USB-накопичувач з образами.&lt;br&gt;Образи повинні знаходитися у корінному каталогу USB-накопичувача.</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="vanished">SD карта захищена від запису.&lt;br&gt;перетягніть перемикач на лівій стороні картки вгору, і спробуйте ще раз.</translation>
    </message>
</context>
</TS>
