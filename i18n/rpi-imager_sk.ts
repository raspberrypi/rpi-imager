<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="sk_SK">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="167"/>
        <source>Error writing to storage</source>
        <translation>Chyba pri zápise na úložisko</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="193"/>
        <location filename="../downloadextractthread.cpp" line="382"/>
        <source>Error extracting archive: %1</source>
        <translation>Chyba pri rozbaľovaní archívu: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="258"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Chyba pri pripájaní partície FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="278"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Operačný systém nepripojil partíciu FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="301"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Chyba pri vstupe do adresára &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error running diskpart: %1</source>
        <translation>Chyba počas behu diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="168"/>
        <source>Error removing existing partitions</source>
        <translation>Chyba pri odstraňovaní existujúcich partiícií</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="194"/>
        <source>Authentication cancelled</source>
        <translation>Zrušená autentifikácia</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="197"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Chyba pri spúšťaní authopen v snahe o získanie prístupu na diskové zariadenie &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="198"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Preverte, prosím, či má &apos;Raspberry Pi Imager&apos; prístup k &apos;vymeniteľným nosičom&apos; v nastaveniach súkromia (pod &apos;súbormi a priečinkami&apos;, prípadne mu udeľte &apos;plný prístup k diskom&apos;).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="219"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Nepodarilo sa otvoriť zariadenie úložiska &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="261"/>
        <source>discarding existing data on drive</source>
        <translation>odstraňujem existujúce údaje z disku</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="281"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>prepisujem prvý a posledny megabajt disku</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="287"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Chyba zápisu pri prepisovaní MBR nulami</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="777"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Chyba pri čítaní z úložiska.&lt;br&gt;Karta SD môže byť poškodená.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="853"/>
        <source>Waiting for FAT partition to be mounted</source>
        <translation>Čakám a pripojenie FAT partície</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="939"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Chyba pri pripájaní partície FAT32</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="961"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Operačný systém nepripojil partíciu FAT32</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="994"/>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="998"/>
        <source>Customizing image</source>
        <translation>Upravujem obraz</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1083"/>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation>Pri vytváraní firstrun.sh na partícii FAT nastala chyba</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1035"/>
        <source>Error writing to config.txt on FAT partition</source>
        <translation>Chyba pri zápise config.txt na FAT partícii</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1100"/>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1114"/>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1137"/>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation>Chyba pri zápise cmdline.txt na FAT partícii</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="431"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Odopretý prístup pri zápise súboru na disk.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="436"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Vyzerá, že máte zapnutý Controlled Folder Access. Pridajte, prosím, rpi-imager.exe a fat32format.exe do zoznamu povolených aplikácií a skúste to znovu.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="442"/>
        <source>Error writing file to disk</source>
        <translation>Chyba pri zápise na disk</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="461"/>
        <source>Error downloading: %1</source>
        <translation>Chyba pri sťahovaní: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="684"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Chyba pri zápise na úložisko (počas volania flush)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="691"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Chyba pri zápise na úložisko (počas volania fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="672"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Stiahnutý súbor je poškodený. Kontrolný súčet nesedí</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="114"/>
        <source>opening drive</source>
        <translation>otváram disk</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="299"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Chyba zápisu pri prepisovaní poslednej časti karty nulami.&lt;br&gt;Karta pravdepodobne udáva nesprávnu kapacitu (a môže byť falošná).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="388"/>
        <source>starting download</source>
        <translation>začína sťahovanie</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="717"/>
        <source>Error writing first block (partition table)</source>
        <translation>Chyba pri zápise prvého bloku (tabuľky partícií)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="796"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Overovanie zápisu skončilo s chybou. Obsah karty SD sa nezhoduje s tým, čo na ňu bolo zapísané.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Chyba pri zápise partícií: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Chyba pri spustení fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Chyba pri spustení fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Chyba pri zisťovaní písmena nového disku</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Neplatné zariadenie: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Chyba pri formátovaní (pomocou udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Chyba pri spustení sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Chyba pri spustení mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Chyba pri spustení mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formátovanie nie je na tejto platforme implementované</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="256"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Kapacita úložiska je nedostatočná&lt;br&gt;Musí byť aspoň %1 GB.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="262"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Vstupný súbor nie je platným obrazom disku.&lt;br&gt;Veľkosť súboru %1 bajtov nie je násobkom 512 bajtov.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="444"/>
        <source>Downloading and writing image</source>
        <translation>Sťahujem a zapisujem obraz</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="577"/>
        <source>Select image</source>
        <translation>Vyberte obraz</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="975"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>otváram súbor s obrazom</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>Chyba pri otváraní súboru s obrazom</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="97"/>
        <source>NO</source>
        <translation>NIE</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="110"/>
        <source>YES</source>
        <translation>ÁNO</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="123"/>
        <source>CONTINUE</source>
        <translation>POKRAČOVAŤ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="135"/>
        <source>QUIT</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="78"/>
        <source>Advanced options</source>
        <translation>Pokročilé možnosti</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="95"/>
        <source>Image customization options</source>
        <translation>Možnosti úprav obrazu</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="103"/>
        <source>for this session only</source>
        <translation>iba pre toto sedenie</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="104"/>
        <source>to always use</source>
        <translation>použiť vždy</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="117"/>
        <source>Disable overscan</source>
        <translation>Vypnúť presnímanie (overscan)</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="122"/>
        <source>Set hostname:</source>
        <translation>Nastaviť meno počítača (hostname):</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="141"/>
        <source>Enable SSH</source>
        <translation>Povoliť SSH</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="160"/>
        <source>Use password authentication</source>
        <translation>Použiť heslo na prihlásenie</translation>
    </message>
    <message>
        <source>Set password for &apos;pi&apos; user:</source>
        <translation type="vanished">Nastaviť heslo pre používateľa &apos;pi&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="170"/>
        <source>Allow public-key authentication only</source>
        <translation>Povoliť iba prihlásenie pomocou verejného kľúča</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;pi&apos;:</source>
        <translation type="vanished">Nastaviť authorized_keys pre &apos;pi&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="185"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="258"/>
        <source>Configure wifi</source>
        <translation>Nastaviť wifi</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="277"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="231"/>
        <location filename="../OptionsPopup.qml" line="297"/>
        <source>Password:</source>
        <translation>Heslo:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="197"/>
        <source>Set username and password</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="216"/>
        <source>Username:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="292"/>
        <source>Hidden SSID</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="313"/>
        <source>Show password</source>
        <translation>Zobraziť heslo</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="318"/>
        <source>Wifi country:</source>
        <translation>Wifi krajina:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="329"/>
        <source>Set locale settings</source>
        <translation>Nastavenia miestnych zvyklostí</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="339"/>
        <source>Time zone:</source>
        <translation>Časové pásmo:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="349"/>
        <source>Keyboard layout:</source>
        <translation>Rozloženie klávesnice:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="359"/>
        <source>Skip first-run wizard</source>
        <translation>Vypnúť sprievodcu prvým spustením</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="366"/>
        <source>Persistent settings</source>
        <translation>Trvalé nastavenia</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="374"/>
        <source>Play sound when finished</source>
        <translation>Po skončení prehrať zvuk</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="378"/>
        <source>Eject media when finished</source>
        <translation>Po skončení vysunúť médium</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="382"/>
        <source>Enable telemetry</source>
        <translation>Povoliť telemetriu</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="395"/>
        <source>SAVE</source>
        <translation>ULOŽIŤ</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>Interná čítačka SD kariet</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="71"/>
        <source>Warning: advanced settings set</source>
        <translation>Varovanie: používajú sa pokročilé možnosti</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="85"/>
        <source>Would you like to apply the image customization settings saved earlier?</source>
        <translation>Chcete použiť uložené nastavenia úprav obrazu?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="94"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NIE, VYČISTIŤ NASTAVENIA</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="106"/>
        <source>YES</source>
        <translation>ÁNO</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="118"/>
        <source>EDIT SETTINGS</source>
        <translation>UPRAVIŤ NASTAVENIA</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="23"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="865"/>
        <source>Are you sure you want to quit?</source>
        <translation>Skutočne chcete skončiť?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="866"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager ešte neskončil.&lt;br&gt;Ste si istý, že chcete skončiť?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="98"/>
        <location filename="../main.qml" line="422"/>
        <source>Operating System</source>
        <translation>Operačný systém</translation>
    </message>
    <message>
        <location filename="../main.qml" line="110"/>
        <source>CHOOSE OS</source>
        <translation>VÝBER OS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="138"/>
        <location filename="../main.qml" line="718"/>
        <source>Storage</source>
        <translation>SD karta</translation>
    </message>
    <message>
        <location filename="../main.qml" line="150"/>
        <location filename="../main.qml" line="1038"/>
        <source>CHOOSE STORAGE</source>
        <translation>VYBERTE SD KARTU</translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>WRITE</source>
        <translation>ZÁPIS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="971"/>
        <source>Writing... %1%</source>
        <translation>Zapisujem... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="230"/>
        <source>CANCEL WRITE</source>
        <translation>ZRUŠIŤ ZÁPIS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="125"/>
        <source>Select this button to change the operating system</source>
        <translation>Pre zmenu operačného systému kliknite na toto tlačidlo</translation>
    </message>
    <message>
        <source>Select this button to change the destination SD card</source>
        <translation type="vanished">Pre zmenu cieľovej SD karty kliknite na toto tlačidlo</translation>
    </message>
    <message>
        <location filename="../main.qml" line="185"/>
        <source>Select this button to start writing the image</source>
        <translation>Kliknutím na toto tlačidlo spustíte zápis</translation>
    </message>
    <message>
        <location filename="../main.qml" line="233"/>
        <location filename="../main.qml" line="968"/>
        <source>Cancelling...</source>
        <translation>Ruším operáciu...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="245"/>
        <source>CANCEL VERIFY</source>
        <translation>ZRUŠIŤ OVEROVANIE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="248"/>
        <location filename="../main.qml" line="991"/>
        <location filename="../main.qml" line="1057"/>
        <source>Finalizing...</source>
        <translation>Ukončujem...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="268"/>
        <source>Select this button to access advanced settings</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="284"/>
        <source>Using custom repository: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="293"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="313"/>
        <source>Language: </source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="336"/>
        <source>Keyboard: </source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="524"/>
        <location filename="../main.qml" line="1025"/>
        <source>Erase</source>
        <translation>Vymazať</translation>
    </message>
    <message>
        <location filename="../main.qml" line="525"/>
        <source>Format card as FAT32</source>
        <translation>Formátovať kartu ako FAT32</translation>
    </message>
    <message>
        <location filename="../main.qml" line="534"/>
        <source>Use custom</source>
        <translation>Použiť vlastný</translation>
    </message>
    <message>
        <location filename="../main.qml" line="535"/>
        <source>Select a custom .img from your computer</source>
        <translation>Použiť vlastný súbor img. na Vašom počítači</translation>
    </message>
    <message>
        <location filename="../main.qml" line="605"/>
        <source>Local file</source>
        <translation>Miestny súbor</translation>
    </message>
    <message>
        <location filename="../main.qml" line="823"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[OCHRANA PROTI ZÁPISU]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="877"/>
        <source>Warning</source>
        <translation>Varovanie</translation>
    </message>
    <message>
        <location filename="../main.qml" line="885"/>
        <source>Preparing to write...</source>
        <translation>Príprava zápisu...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="898"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Všetky existujúce dáta na &apos;%1&apos; budú odstránené.&lt;br&gt;Naozaj chcete pokračovať?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="909"/>
        <source>Update available</source>
        <translation>Je dostupná aktualizácia</translation>
    </message>
    <message>
        <location filename="../main.qml" line="910"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Je dostupná nová verzia Imagera.&lt;br&gt;Chcete prejsť na webovú stránku s programom a stiahnuť ho?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1001"/>
        <source>Preparing to write... (%1)</source>
        <translation>Príprava zápisu... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1026"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; bola vymazaná&lt;br&gt;&lt;br&gt;Teraz môžete odstrániť SD kartu z čítačky</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1028"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; bol zapísaný na &lt;b&gt;%2&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1096"/>
        <source>Error parsing os_list.json</source>
        <translation>Chyba pri spracovaní os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1250"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Najprv pripojte USB kľúč, ktorý obsahuje diskové obrazy.&lt;br&gt;Obrazy sa musia nachádzať v koreňovom priečinku USB kľúča.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1266"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SD karta je chránená proti zápisu.&lt;br&gt;Presuňte prepínač zámku na ľavej strane karty smerom hore a skúste to znova.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="482"/>
        <source>Back</source>
        <translation>Späť</translation>
    </message>
    <message>
        <location filename="../main.qml" line="163"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Pre zmenu cieľového zariadenia úložiska kliknite na toto tlačidlo</translation>
    </message>
    <message>
        <location filename="../main.qml" line="483"/>
        <source>Go back to main menu</source>
        <translation>Prejsť do hlavnej ponuky</translation>
    </message>
    <message>
        <location filename="../main.qml" line="600"/>
        <source>Released: %1</source>
        <translation>Vydané: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="603"/>
        <source>Cached on your computer</source>
        <translation>Uložené na počítači</translation>
    </message>
    <message>
        <location filename="../main.qml" line="607"/>
        <source>Online - %1 GB download</source>
        <translation>Online %1 GB na stiahnutie</translation>
    </message>
    <message>
        <location filename="../main.qml" line="769"/>
        <location filename="../main.qml" line="821"/>
        <location filename="../main.qml" line="827"/>
        <source>Mounted as %1</source>
        <translation>Pripojená ako %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="950"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Chyba pri sťahovaní zoznamu OS z Internetu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="994"/>
        <source>Verifying... %1%</source>
        <translation>Overujem... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1017"/>
        <source>Error</source>
        <translation>Chyba</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1024"/>
        <source>Write Successful</source>
        <translation>Zápis úspešne skončil</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1030"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; bol zapísaný na &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Teraz môžete odstrániť SD kartu z čítačky</translation>
    </message>
</context>
</TS>
