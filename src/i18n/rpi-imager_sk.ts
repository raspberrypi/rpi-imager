<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="sk_SK">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Chyba pri rozbaľovaní archívu: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Chyba pri pripájaní partície FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Operačný systém nepripojil partíciu FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Chyba pri vstupe do adresára &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Chyba pri čítaní z úložiska.&lt;br&gt;Karta SD môže byť poškodená.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Overovanie zápisu skončilo s chybou. Obsah karty SD sa nezhoduje s tým, čo na ňu bolo zapísané.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>odpájam jednotku</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>otváram jednotku</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Chyba počas behu diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Chyba pri odstraňovaní existujúcich partiícií</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Zrušená autentifikácia</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Chyba pri spúšťaní authopen v snahe o získanie prístupu na diskové zariadenie &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Preverte, prosím, či má &apos;Raspberry Pi Imager&apos; prístup k &apos;vymeniteľným nosičom&apos; v nastaveniach súkromia (pod &apos;súbormi a priečinkami&apos;, prípadne mu udeľte &apos;plný prístup k diskom&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Nepodarilo sa otvoriť zariadenie úložiska &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>odstraňujem existujúce údaje na jednotke</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>prepisujem prvý a posledný megabajt na jednotke</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Chyba zápisu pri prepisovaní MBR nulami</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Chyba zápisu pri prepisovaní poslednej časti karty nulami.&lt;br&gt;Karta pravdepodobne udáva nesprávnu kapacitu (a môže byť falošná).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>začína sťahovanie</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Chyba pri sťahovaní: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Odopretý prístup pri zápise súboru na disk.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Vyzerá, že máte zapnutý Controlled Folder Access. Pridajte, prosím, rpi-imager.exe a fat32format.exe do zoznamu povolených aplikácií a skúste to znovu.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Chyba pri zápise na disk</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Chyba pri zápise na úložisko (počas volania flush)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Chyba pri zápise na úložisko (počas volania fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Chyba pri zápise prvého bloku (tabuľky partícií)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Chyba pri čítaní z úložiska.&lt;br&gt;Karta SD môže byť poškodená.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Overovanie zápisu skončilo s chybou. Obsah karty SD sa nezhoduje s tým, čo na ňu bolo zapísané.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Upravujem obraz</translation>
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
        <translation>Chyba pri zápise partícií: %1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>Chyba pri spustení fat32format</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>Chyba pri spustení fat32format: %1</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>Chyba pri zisťovaní písmena novej jednotky</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>Neplatné zariadenie: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Chyba pri formátovaní (pomocou udisks2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Formátovanie nie je na tejto platforme implementované</translation>
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
        <translation type="unfinished">SD karta</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished">Nenašli sa žiadne úložné zariadenia</translation>
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
        <translation type="unfinished">Pripojená ako %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[OCHRANA PROTI ZÁPISU]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD karta je chránená proti zápisu.&lt;br&gt;Presuňte prepínač zámku na ľavej strane karty smerom hore a skúste to znova.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">VYBERTE ZARIADENIE</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Zariadenie Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Kapacita úložiska je nedostatočná&lt;br&gt;Musí byť aspoň %1 GB.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Vstupný súbor nie je platným obrazom disku.&lt;br&gt;Veľkosť súboru %1 bajtov nie je násobkom 512 bajtov.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Sťahujem a zapisujem obraz</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Vyberte obraz</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Chyba pri synchronizácii času. Vyskúšam to znova o 3 sekundy</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>Na vašom sieťovom prepínači je povolený protokol STP. Získanie IP bude trvať dlhý čas.</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Chcete predvyplniť heslo pre wifi zo systémovej kľúčenky?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>otváram súbor s obrazom</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Chyba pri otváraní súboru s obrazom</translation>
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
        <translation>NIE</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ÁNO</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>POKRAČOVAŤ</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>UKONČIŤ</translation>
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
        <translation type="unfinished">Operačný systém</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Späť</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Prejsť do hlavnej ponuky</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Vydané: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">Uložené na počítači</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Miestny súbor</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Online %1 GB na stiahnutie</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Najprv pripojte USB kľúč, ktorý obsahuje diskové obrazy.&lt;br&gt;Obrazy sa musia nachádzať v koreňovom priečinku USB kľúča.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Nastaviť meno počítača (hostname):</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Nastaviť meno používateľa a heslo</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Meno používateľa:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Heslo:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Nastaviť wifi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">Skryté SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Wifi krajina:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Nastavenia miestnych zvyklostí</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Časové pásmo:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Rozloženie klávesnice:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Po skončení prehrať zvuk</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Po skončení vysunúť médium</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Povoliť telemetriu</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Úpravy OS</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Všeobecné</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Služby</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Možnosti</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>ULOŽIŤ</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">Povoliť SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Použiť heslo na prihlásenie</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Povoliť iba prihlásenie pomocou verejného kľúča</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Nastaviť authorized_keys pre &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">SPUSTIŤ SSH-KEYGEN</translation>
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
        <translation>Chceli by ste použiť nastavenia, ktoré upravujú operačný systém?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NIE</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NIE, VYČISTIŤ NASTAVENIA</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>ÁNO</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>UPRAVIŤ NASTAVENIA</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Zariadenie Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Vyberte toto tlačidlo pre výber cieľového Raspberry Pi</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Operačný systém</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>VYBERTE OS</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Pre zmenu operačného systému kliknite na toto tlačidlo</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>SD karta</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>VYBERTE SD KARTU</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Pre zmenu cieľového zariadenia úložiska kliknite na toto tlačidlo</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>ZRUŠIŤ ZÁPIS</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Ruším operáciu...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>ZRUŠIŤ OVEROVANIE</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Ukončujem...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Ďalej</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Kliknutím na toto tlačidlo spustíte zápis</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Používa sa vlastný repozitár: %1</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>Sieť ešte nie je pripravená</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Ovládanie pomocou klávesnice: &lt;tabulátor&gt; prechod na ďalšie tlačidlo &lt;medzerník&gt; stlačenie tlačidla/výber položky &lt;šípka hore/dole&gt; posun hore/dole v zoznamoch</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Jazyk:</translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Klávesnica: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Skutočne chcete skončiť?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager ešte neskončil.&lt;br&gt;Ste si istý, že chcete skončiť?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Varovanie</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Príprava zápisu...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Všetky existujúce dáta na &apos;%1&apos; budú odstránené.&lt;br&gt;Naozaj chcete pokračovať?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Je dostupná aktualizácia</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Je dostupná nová verzia Imagera.&lt;br&gt;Chcete prejsť na webovú stránku s programom a stiahnuť ho?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Zapisujem... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Overujem... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Príprava zápisu... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Chyba</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Zápis úspešne skončil</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Vymazať</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; bola vymazaná&lt;br&gt;&lt;br&gt;Teraz môžete odstrániť SD kartu z čítačky</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; bol zapísaný na &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Teraz môžete odstrániť SD kartu z čítačky</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formátovať kartu ako FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Použiť vlastný</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Použiť vlastný súbor img. na Vašom počítači</translation>
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
