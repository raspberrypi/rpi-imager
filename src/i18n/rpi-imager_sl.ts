<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="sl_SI">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Napaka razširjanja arhiva: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Napaka priklopa FAT32 particije</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Operacijski sistem, ni priklopil FAT32 particije</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Napaka spremembe direktorija &apos;%1%&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Napaka branja iz diska.&lt;br&gt;SD kartica/disk je mogoče v okvari.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Preverjanje pisanja spodletelo. Vsebina diska je drugačna, od tega, kar je bilo nanj zapisano.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>Odpiranje pogona</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Napaka zagona diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Napaka odstranjevanja obstoječih particij</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Avtentifikacija preklicana</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Napaka zagona authopen za pridobitev dostopa do naprave diska &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Prosim preverite če ima &apos;Raspberry Pi Imager&apos; pravico dostopa do &apos;odstranljivih mediev&apos; pod nastavitvami zasebnosti (pod &apos;datoteke in mape&apos; ali pa mu dajte &apos;popolni dostop do diska&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Ne morem odpreti diska &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>Brisanje obstoječih podatkov na disku</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>Ničenje prvega in zadnjega MB diska</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Napaka zapisovanja med ničenjem MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Napaka ničenja zadnjega dela diska.&lt;br&gt;Disk morebiti sporoča napačno velikost(možen ponaredek).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>Začetek prenosa</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Napaka prenosa:%1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Napaka zavrnitve dostopa med pisanjem na disk.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Izgleda, da jevklopljen nadzor dostopa do map. Prosim dodajte oba rpi-imager.exe in fat32format.exe na seznam dovoljenih aplikacij in poizkusite znova.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Napaka pisanja na disk</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Napaka pisanja na disk (med brisanjem)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Napaka pisanja na disk (med fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Napaka pisanja prvega bloka (particijska tabela)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Napaka branja iz diska.&lt;br&gt;SD kartica/disk je mogoče v okvari.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Preverjanje pisanja spodletelo. Vsebina diska je drugačna, od tega, kar je bilo nanj zapisano.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Prilagajanje slike diska</translation>
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
        <translation>Napaka izdelave particij: %1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>Napaka zagona fat32format</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>Napaka delovanja fat32format: %1</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>Napaka določitve nove črke pogona</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>Neveljavna naprava: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Napaka fromatiranja (z uporabo udisks2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatiranje ni implemntirano za to platformo</translation>
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
        <translation type="unfinished">SD kartica ali USB disk</translation>
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
        <translation type="unfinished">Priklopljen kot %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[ZAŠČITENO PRED PISANJEM]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD kartica je zaščitena pred pisanjem.&lt;br&gt;Premaknite stikalo zaklepanja, na levi strani kartice in poizkusite znova.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Kapaciteta diska ni zadostna.&lt;br&gt;Biti mora vsaj %1 GB.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Vhodna datoteka ni veljavna slika diska.&lt;br&gt;Velikost datoteke %1 bajtov ni večkratnik od 512 bajtov.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Prenašanje in zapisovanje slike</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Izberite sliko</translation>
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
        <translation>A bi želeli uporabiti WiFi geslo iz kjučev tega sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>Odpiranje slike diska</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Napaka odpiranja slike diska</translation>
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
        <translation>NE</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>DA</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>NADALJUJ</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>IZHOD</translation>
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
        <translation type="unfinished">Operacijski Sistem</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Nazaj</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Pojdi nazaj na glavni meni</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Izdano: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">Predpolnjeno na vaš računalnik</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Lokalna datoteka</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Iz spleta - %1 GB prenos</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Najprej prikopite USB disk, ki vsebuje slike diskov.&lt;br&gt;Slike diskov se morajo nahajati v korenski mapi USB diska.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Ime naprave:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Geslo:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Nastavi WiFi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">WiFi je v državi:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Nastavitve jezika</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Časovni pas:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Razporeditev tipkovnice:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Zaigraj zvok, ko končaš</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Izvrzi medij, ko končaš</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Omogoči telemetrijo</translation>
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
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Services</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Options</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>SHRANI</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">Omogoči SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Uporabi geslo za avtentifikacijo</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Dovoli le avtentifikacijo z javnim kjučem</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Nastavi authorized_keys za &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished"></translation>
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
        <translation>NE</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NE, POBRIŠI NASTAVITVE</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>DA</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>UREDI NASTAVITVE</translation>
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
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Operacijski Sistem</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>IZBERI OS</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Izberite ta gumb za menjavo operacijskega sistema</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>SD kartica ali USB disk</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>IZBERI DISK</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Uporabite ta gumb za spremembo ciljnega diska</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>PREKINI ZAPISOVANJE</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Prekinjam...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>PREKINI PREVERJANJE</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Zakjučujem...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Izberite za gumb za začetek pisanja slike diska</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Uporabljam repozitorij po meri: %1</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navigacija s tipkovnico: &lt;tab&gt; pojdi na naslednji gumb &lt;preslednica&gt; pritisni gumb/izberi element &lt;puščica gor/dol&gt; premakni gor/dol po seznamu</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Jezik: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Tipkovnica: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>A ste prepričani, da želite končat?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager je še vedno zaposlen.&lt;br&gt;A ste prepričani, da želite končati?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Opozorilo</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Priprava na pisanje...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>VSI obstoječi podatki na &apos;%1&apos; bodo izbrisani.&lt;br&gt;A ste prepričani, da želite nadaljevati?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Posodobitev na voljo</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Na voljo je nova verzija tega programa. &lt;br&gt;Želite obiskati spletno stran za prenos?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Pišem...%1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Preverjam... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Priprava na zapisovanje... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Napaka</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Zapisovanje uspešno</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Odstrani</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; je pobrisan&lt;br&gt;&lt;br&gt;Sedaj lahko odstranite SD kartico iz čitalca oz iztaknete USB disk</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; je zapisan na &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Sedaj lahko odstranite SD kartico iz čitalca oz iztaknete USB disk</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formatiraj disk v FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Uporabi drugo</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Izberite drug .img iz vašega računalnika</translation>
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
