<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="pl_PL">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="171"/>
        <source>Error writing to storage</source>
        <translation>Błąd zapisu do pamięci</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>Błąd wypakowywania archiwum: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Błąd montowania partycji FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>System operacyjny nie zamontował partycji FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Błąd zmieniana katalogu &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error running diskpart: %1</source>
        <translation>Błąd podczas uruchamiania diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="168"/>
        <source>Error removing existing partitions</source>
        <translation>Błąd podczas usuwania istniejących partycji</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="194"/>
        <source>Authentication cancelled</source>
        <translation>Uwierzytelnianie anulowane</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="197"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Błąd podczas uruchamiania authopen w celu uzyskania dostępu do urządzenia dyskowego &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="198"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Sprawdź, czy program „Raspberry Pi Imager” ma dostęp do „woluminów wymiennych” w ustawieniach prywatności (w sekcji „pliki i foldery” lub przyznaj mu „pełny dostęp do dysku”).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="220"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Nie można otworzyć urządzenia pamięci masowej &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="262"/>
        <source>discarding existing data on drive</source>
        <translation>usuwanie istniejących danych na dysku</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>zerowanie pierwszego i ostatniego megabajta dysku</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="288"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Błąd zapisu podczas zerowania MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="779"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Błąd odczytu z pamięci.&lt;br&gt;Karta pamięci może być uszkodzona.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="855"/>
        <source>Waiting for FAT partition to be mounted</source>
        <translation>Oczekiwanie na zamontowanie partycji FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="941"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Błąd montowania partycji FAT32</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="963"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>System operacyjny nie zamontował partycji FAT32</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="996"/>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation>Nie można zapisać niestandardowych ustawień. Plik &apos;%1&apos; nie istnieje.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1000"/>
        <source>Customizing image</source>
        <translation>Dostosowywanie obrazu</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1085"/>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation>Błąd podczas tworzenia pliku firstrun.sh na partycji FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1037"/>
        <source>Error writing to config.txt on FAT partition</source>
        <translation>Błąd podczas zapisu pliku config.txt na partycji FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1102"/>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation>Błąd podczas tworzenia pliku cloudinit (dane użytkownika) na partycji FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1116"/>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation>Błąd podczas tworzenia pliku cloudinit (ustawienia sieci) na partycji FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1139"/>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation>Bład podczas zapisu do cmdline.txt na partycji FAT</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="432"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Odmowa dostępu podczas próby zapisu pliku na dysku.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="437"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Wygląda na to że włączono Kontrolowany dostęp do folderu. Proszę dodać rpi-imager.exe oraz fat32format.exe do listy dozwolonych aplikacji i spróbuj ponownie.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="444"/>
        <source>Error writing file to disk</source>
        <translation>Błąd zapisu pliku na dysku</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="463"/>
        <source>Error downloading: %1</source>
        <translation>Błąd pobierania: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Błąd zapisu do pamięci (podczas synchronizacji danych)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="693"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Błąd zapisu do pamięci (podczas fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="674"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Pobrane dane są uszkodzone. Sumy kontrolne się nie zgadzają</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="114"/>
        <source>opening drive</source>
        <translation>otwieranie dysku</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="300"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Błąd zapisu podczas próby wyzerowania końcowej części karty pamięci.&lt;br&gt;Karta może zgłaszać nieprawidłową pojemność (możliwe że jest podrobiona).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="389"/>
        <source>starting download</source>
        <translation>rozpoczynanie pobierania</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <source>Error writing first block (partition table)</source>
        <translation>Błąd zapisu pierwszego bloku (tabela partycji)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="798"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Nieudana weryfikacja zapisu. Zawartość karty pamięci różni się od tego, co na niej zapisano.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Błąd partycjonowania: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Błąd podczas uruchamiania fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>fat32format zwrócił błąd: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Błąd podczas ustalania nowej litery dysku</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Nieprawidłowe urządzenie: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Błąd formatowania (przez udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Błąd podczas uruchamiania sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>Proces partycjonowania nie utworzył spodziewanej partycji FAT %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Błąd podczas uruchamiania mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>mkfs.fat zwrócił błąd: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatowanie jest niezaimplementowane dla tej platformy</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="257"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Pojemność pamięci nie jest wystarczająco duża.&lt;br&gt;Musi mieć przynajmniej %1 GB.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="263"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Plik wejściowy nie jest prawidłowym obrazem dysku.&lt;br&gt;Waga pliku (%1 bajtów) nie jest wielokrotnością 512 bajtów.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="445"/>
        <source>Downloading and writing image</source>
        <translation>Pobieranie i zapisywanie obrazu</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="578"/>
        <source>Select image</source>
        <translation>Wybierz obraz</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="979"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Czy chcesz wstępnie wypełnić hasło Wi-Fi z systemowego pęku kluczy?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>otwieranie obrazu dysku</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>Błąd otwierania obrazu dysku</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>NIE</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>TAK</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
        <source>CONTINUE</source>
        <translation>KONTYNUUJ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="130"/>
        <source>QUIT</source>
        <translation>WYJDŹ</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="79"/>
        <source>Advanced options</source>
        <translation>Ustawienia zaawansowane</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="96"/>
        <source>Image customization options</source>
        <translation>Opcje dostosowania obrazu</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="104"/>
        <source>for this session only</source>
        <translation>tylko dla tej sesji</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="105"/>
        <source>to always use</source>
        <translation>zawsze używaj</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="119"/>
        <source>Set hostname:</source>
        <translation>Ustaw nazwę hosta:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="138"/>
        <source>Enable SSH</source>
        <translation>Włącz SSH</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="160"/>
        <source>Use password authentication</source>
        <translation>Użyj uwierzytelniania hasłem</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="170"/>
        <source>Allow public-key authentication only</source>
        <translation>Zezwalaj tylko na uwierzytelnianie za pomocą klucza publicznego</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="188"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>Ustaw authorized_keys dla &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="261"/>
        <source>Configure wireless LAN</source>
        <translation>Skonfiguruj bezprzewodową sieć LAN</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="280"/>
        <source>SSID:</source>
        <translation>Identyfikator SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="234"/>
        <location filename="../OptionsPopup.qml" line="300"/>
        <source>Password:</source>
        <translation>Hasło:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="200"/>
        <source>Set username and password</source>
        <translation>Ustaw nazwę użytkownika oraz hasło</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="219"/>
        <source>Username:</source>
        <translation>Nazwa użytkownika:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="295"/>
        <source>Hidden SSID</source>
        <translation>Sieć ukryta</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="316"/>
        <source>Show password</source>
        <translation>Pokaż hasło</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="321"/>
        <source>Wireless LAN country:</source>
        <translation>Kraj bezprzewodowej sieci LAN:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="332"/>
        <source>Set locale settings</source>
        <translation>Skonfiguruj ustawienia lokalne</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="342"/>
        <source>Time zone:</source>
        <translation>Strefa czasowa:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="352"/>
        <source>Keyboard layout:</source>
        <translation>Układ klawiatury:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="365"/>
        <source>Persistent settings</source>
        <translation>Trwałe ustawienia</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="373"/>
        <source>Play sound when finished</source>
        <translation>Odtwórz dźwięk po zakończeniu</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="377"/>
        <source>Eject media when finished</source>
        <translation>Po zakończeniu wysuń nośnik</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="381"/>
        <source>Enable telemetry</source>
        <translation>Włącz telemetrię</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="394"/>
        <source>SAVE</source>
        <translation>ZAPISZ</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>Wewnętrzny czytnik kart SD</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="72"/>
        <source>Warning: advanced settings set</source>
        <translation>Ostrzeżenie: ustawiono ustawienia zaawansowane</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="86"/>
        <source>Would you like to apply the image customization settings saved earlier?</source>
        <translation>Czy chcesz zastosować zapisane wcześniej ustawienia obrazu?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="95"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NIE, WYCZYŚĆ USTAWIENIA</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="105"/>
        <source>YES</source>
        <translation>TAK</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>EDIT SETTINGS</source>
        <translation>EDYTUJ USTAWIENIA</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../main.qml" line="99"/>
        <location filename="../main.qml" line="399"/>
        <source>Operating System</source>
        <translation>System Operacyjny</translation>
    </message>
    <message>
        <location filename="../main.qml" line="111"/>
        <source>CHOOSE OS</source>
        <translation>WYBIERZ SYSTEM</translation>
    </message>
    <message>
        <location filename="../main.qml" line="123"/>
        <source>Select this button to change the operating system</source>
        <translation>Wybierz ten przycisk, aby zmienić system operacyjny</translation>
    </message>
    <message>
        <location filename="../main.qml" line="135"/>
        <location filename="../main.qml" line="713"/>
        <source>Storage</source>
        <translation>Pamięć masowa</translation>
    </message>
    <message>
        <location filename="../main.qml" line="147"/>
        <location filename="../main.qml" line="1038"/>
        <source>CHOOSE STORAGE</source>
        <translation>WYBIERZ PAMIĘĆ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="173"/>
        <source>WRITE</source>
        <translation>WGRAJ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="177"/>
        <source>Select this button to start writing the image</source>
        <translation>Wybierz ten przycisk, aby rozpocząć zapisywanie obrazu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="218"/>
        <source>CANCEL WRITE</source>
        <translation>ANULUJ ZAPIS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <location filename="../main.qml" line="965"/>
        <source>Cancelling...</source>
        <translation>Anulowanie...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="229"/>
        <source>CANCEL VERIFY</source>
        <translation>ANULUJ WERYFIKACJĘ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="232"/>
        <location filename="../main.qml" line="988"/>
        <location filename="../main.qml" line="1057"/>
        <source>Finalizing...</source>
        <translation>Kończenie...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="247"/>
        <source>Select this button to access advanced settings</source>
        <translation>Wybierz ten przycisk, aby uzyskać dostęp do ustawień zaawansowanych</translation>
    </message>
    <message>
        <location filename="../main.qml" line="261"/>
        <source>Using custom repository: %1</source>
        <translation>Używanie niestandardowego repozytorium: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="270"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Nawigacja za pomocą klawiatury: &lt;tab&gt; przejdź do następnego przycisku &lt;space&gt; naciśnij przycisk/wybierz element &lt;arrow up/down&gt; przejdź w górę/w dół w listach</translation>
    </message>
    <message>
        <location filename="../main.qml" line="290"/>
        <source>Language: </source>
        <translation>Język: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="313"/>
        <source>Keyboard: </source>
        <translation>Klawiatura: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="505"/>
        <location filename="../main.qml" line="1022"/>
        <source>Erase</source>
        <translation>Wyczyść</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Format card as FAT32</source>
        <translation>Sformatuj kartę pamięci do FAT32</translation>
    </message>
    <message>
        <location filename="../main.qml" line="515"/>
        <source>Use custom</source>
        <translation>Niestandardowy obraz</translation>
    </message>
    <message>
        <location filename="../main.qml" line="516"/>
        <source>Select a custom .img from your computer</source>
        <translation>Wybierz plik .img z twojego komputera</translation>
    </message>
    <message>
        <location filename="../main.qml" line="461"/>
        <source>Back</source>
        <translation>Cofnij</translation>
    </message>
    <message>
        <location filename="../main.qml" line="157"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Wybierz ten przycisk aby zmienić docelowe urządzenie</translation>
    </message>
    <message>
        <location filename="../main.qml" line="462"/>
        <source>Go back to main menu</source>
        <translation>Wróc do menu głównego</translation>
    </message>
    <message>
        <location filename="../main.qml" line="628"/>
        <source>Released: %1</source>
        <translation>Premiera: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="638"/>
        <source>Cached on your computer</source>
        <translation>Już pobrane na twoim komputerze</translation>
    </message>
    <message>
        <location filename="../main.qml" line="640"/>
        <source>Local file</source>
        <translation>Plik lokalny</translation>
    </message>
    <message>
        <location filename="../main.qml" line="641"/>
        <source>Online - %1 GB download</source>
        <translation>Do pobrania - %1 GB</translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <location filename="../main.qml" line="818"/>
        <location filename="../main.qml" line="824"/>
        <source>Mounted as %1</source>
        <translation>Zamontowano jako %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="820"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[TYLKO DO ODCZYTU]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="862"/>
        <source>Are you sure you want to quit?</source>
        <translation>Czy na pewno chcesz wyjść?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager nadal wykonuje akcje.&lt;br&gt;Czy na pewno chcesz wyjść?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="874"/>
        <source>Warning</source>
        <translation>Ostrzeżenie</translation>
    </message>
    <message>
        <location filename="../main.qml" line="882"/>
        <source>Preparing to write...</source>
        <translation>Przygotowywanie do zapisu...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Update available</source>
        <translation>Dostępna aktualizacja</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Dostępna jest nowsza wersja Imager.&lt;br&gt;Czy chcesz otworzyć stronę aby ją pobrać?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="968"/>
        <source>Writing... %1%</source>
        <translation>Zapisywanie... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="895"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Wszystkie dane na dysku &apos;%1&apos; zostaną usunięte.&lt;br&gt;Jesteś pewien że chcesz kontynuować?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="947"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Nie można pobrać listy systemów operacyjnych z Internetu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="991"/>
        <source>Verifying... %1%</source>
        <translation>Weryfikowanie... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="998"/>
        <source>Preparing to write... (%1)</source>
        <translation>Przygotowanie do zapisu... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1014"/>
        <source>Error</source>
        <translation>Błąd</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1021"/>
        <source>Write Successful</source>
        <translation>Udany zapis</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1023"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; został wymazany&lt;br&gt;&lt;br&gt;Możesz już wyjąć kartę pamięci z czytnika</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1030"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; został zapisany do &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Możesz już wyjąć kartę pamięci z czytnika</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1098"/>
        <source>Error parsing os_list.json</source>
        <translation>Nieudane parsowanie pliku os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1271"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Najpierw podłącz pamięć USB zawierającą obrazy.&lt;br&gt;Obrazy muszą znajdować się w folderze głównym pamięci USB.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1287"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>Karta SD jest chroniona przed zapisem.&lt;br&gt;Przesuń przełącznik blokady po lewej stronie karty w górę i spróbuj ponownie.</translation>
    </message>
</context>
</TS>
