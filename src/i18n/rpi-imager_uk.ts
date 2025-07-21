<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="uk_UA">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Помилка розпакування архіва: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Помилка монтування FAT32 розділу</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Операційна система не монтувала FAT32 розділ</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Помилка при зміні каталогу на &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Помилка читання накопичувача.&lt;br&gt;SD-карта пам&apos;яті може бути пошкоджена.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Помилка перевірки запису. Зміст SD-карти пам&apos;яті відрізняється від того, що було записано туди.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>диск від&apos;єднується</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>диск відкривається</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Помилка при виконанні diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Помилка при видаленні існуючих розділів</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Аутентифікація скасована</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Помилка виконання authopen для отримання доступу до пристроя &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Переконайтеся, що у Raspberry Pi Imager у налаштуваннях приватності (у розділі &quot;файли та каталоги&quot;) є доступ до змінних розділів. Або дайте програмі доступ до усього диску.</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Не вдалося відкрити накопичувач &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>видалення існуючих даних на диску</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>обнулювання першого і останнього мегабайта диску</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Помилка при обнулюванні MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Помилка запису під час обнулювання останнього розділу карти пам&apos;яті.&lt;br&gt;Можливо заявлений об&apos;єм карти не збігається з реальним (можливо карта є підробленою).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>початок завантаження</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Помилка завантаження: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Помилка доступу при записі файлу на диск.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Помилка запису файлу на диск</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Помилка запису на накопичувач (при скидуванні)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Помилка запису на накопичувач (при виконанні fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Помилка під час запису першого блоку (таблиця розділів)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Помилка читання накопичувача.&lt;br&gt;SD-карта пам&apos;яті може бути пошкоджена.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Помилка перевірки запису. Зміст SD-карти пам&apos;яті відрізняється від того, що було записано туди.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Налаштування образа</translation>
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
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add rpi-imager.exe to the list of allowed apps and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Помилка форматування (через udisks2)</translation>
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
    <message>
        <source>Cannot format device: insufficient permissions and udisks2 not available</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation type="unfinished">Накопичувач</translation>
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
        <translation type="unfinished">Примонтовано як %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">ЗАХИЩЕНО ВІД ЗАПИСУ</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD карта захищена від запису.&lt;br&gt;перетягніть перемикач на лівій стороні картки вгору, і спробуйте ще раз.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">ОБРАТИ ПРИСТРІЙ</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Пристрій Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Місця на накопичувачі недостатньо.&lt;br&gt;Треба, щоб було хоча б %1 ГБ.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Обраний файл не є правильним образом диску.&lt;br&gt;Розмір файла %1 байт не є кратним 512 байт.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Завантаження і запис образу</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Обрати образ</translation>
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
        <translation>Вказати пароль від Wi-Fi автоматично із системного ланцюга ключів?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>відкривання файлу образа</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Помилка при відкриванні файлу образу</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error reading from image file</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error writing to device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Failed to read complete image file</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <source>NO</source>
        <translation>НІ</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ТАК</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>ПРОДОВЖИТИ</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>ВИЙТИ</translation>
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
        <translation type="unfinished">Операційна система</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Назад</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Повернутися у головне меню</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Випущено: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">Кешовано на вашому комп&apos;ютері</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Локальний файл</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Онлайн - потрібно завантажити %1 ГБ</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Спочатку під&apos;єднайте USB-накопичувач з образами.&lt;br&gt;Образи повинні знаходитися у корінному каталогу USB-накопичувача.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Встановити ім&apos;я хосту:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Встановити ім&apos;я користувача і пароль</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Ім&apos;я користувача:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Пароль:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Налаштувати бездротову LAN мережу</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">Прихована SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Країна бездротової LAN мережі:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Змінити налаштування регіону</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Часова зона:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Розкладка клавіатури:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Відтворити звук після завершення</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Витягнути накопичувач після завершення</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Увімкнути телеметрію</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Налаштування ОС</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Загальні</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Сервіси</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Налаштування</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>ЗБЕРЕГТИ</translation>
    </message>
    <message>
        <source>CANCEL</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in General and Services tabs</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in General tab</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in Services tab</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">Увімкнути SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Використовувати аутентефікацію через пароль</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Дозволити аутентифікацію лише через публічні ключі</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Встановити authorized_keys для &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">ЗАПУСТИТИ SHH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Paste your SSH public key here.
Supported formats: ssh-rsa, ssh-ed25519, ssh-dss, ecdsa-sha2-nistp, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates
Example: ssh-rsa AAAAB3NzaC1yc2E... user@hostname</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Чи бажаєте ви прийняти налаштування ОС?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>НІ</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>НІ, ОЧИСТИТИ НАЛАШТУВАННЯ</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ТАК</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>РЕДАГУВАТИ НАЛАШТУВАННЯ</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager, версія %1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Пристрій Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Оберіть цю кнопку, щоб обрати модель вашої Raspberry Pi</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Операційна система</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>ОБРАТИ ОС</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Натисніть на цю кнопку, щоб змінити операційну систему</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Накопичувач</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>ОБРАТИ НАКОПИЧУВАЧ</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Натисніть цю кнопку, щоб змінити пристрій призначення</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>СКАСУВАТИ ЗАПИСУВАННЯ</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Скасування...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>СКАСУВАТИ ПЕРЕВІРКУ</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Завершення...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Далі</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Натисніть цю кнопку, щоб розпочати запис образу</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Користуючись власним репозиторієм: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Навігація клавіатурою: клавіша &lt;Tab&gt; переміститися на наступну кнопку, клавіша &lt;Пробіл&gt; натиснути кнопку/обрати елемент, клавіши з &lt;стрілками вниз/вгору&gt; переміститися вниз/вгору по списку</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Мова: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Клавіатура: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Бажаєте вийти?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager все ще зайнятий.&lt;br&gt;Ви впевнені, що бажаєте вийти?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Увага</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Підготовка до запису...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Усі уснуючі дані у &apos;%1&apos; будуть видалені.&lt;br&gt; Ви впевнені, що бажаєте продовжити?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Доступно оновлення</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Доступна нова версія Imager.&lt;br&gt;Бажаєте завітати на сайт та завантажити її?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Записування...%1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Перевірка...%1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Підготовка до запису... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Помилка</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Успішно записано</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Видалити</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; був успішно видалено.br&gt;&lt;br&gt; тепер можна дістати SD карту із считувача</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Записування &lt;b&gt;%1&lt;/b&gt; на &lt;b&gt;%2&lt;/b&gt; виконано &lt;br&gt;&lt;br&gt; Тепер можна дістати SD карту із считувача</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Форматувати карту у FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Власний образ</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Обрати власний .img з вашого комп&apos;ютера</translation>
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
