<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="pl_PL">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Błąd podczas wypakowywania archiwum: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Błąd montowania partycji FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>System operacyjny nie zamontował partycji FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Błąd przejścia do katalogu &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Błąd odczytu z pamięci masowej.&lt;br&gt;Karta SD może być uszkodzona</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Weryfikacja zapisu nie powiodła się. Zawartość karty SD różni się od tego, co zostało na niej zapisane.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>odmontowywanie dysku</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>otwieranie dysku</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Błąd uruchomienia diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Błąd usuwania istniejących partycji</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Uwierzytelnianie anulowane</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Błąd uruchomienia authopen w celu uzyskania dostępu do urządzenia dyskowego &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>Not sure if current macOS has that option (or if it got moved/renamed)</translatorcomment>
        <translation type="unfinished">Sprawdź, czy &apos;Raspberry Pi Imager&apos; ma dostęp do &apos;woluminów wymiennych&apos; w ustawieniach prywatności (w &apos;plikach i folderach&apos; lub alternatywnie daj mu &apos;pełny dostęp do dysku&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Nie można otworzyć urządzenia pamięci masowej &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>usuwanie istniejących danych na dysku</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>zerowanie pierwszego oraz ostatniego megabajta dysku</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Błąd zapisu podczas zerowania MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Błąd zapisu podczas próby wyzerowania ostatniej części karty.&lt;br&gt;;Karta może pokazywać nieprawidłową pojemność (możliwe podróbki).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>rozpoczęcie pobierania</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Błąd pobierania: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Odmowa dostępu podczas próby zapisu pliku na dysk.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>Dostęp do &quot;Folderów Kontrolowanych&quot; wydaje się być włączony. Dodaj rpi-imager.exe i fat32format.exe do listy dozwolonych aplikacji i spróbuj ponownie.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Błąd zapisu pliku na dysk</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Błąd zapisu podczas wykonywania: flushing</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Błąd zapisu podczas wykonywania: fsync</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Błąd zapisu pierwszego bloku (tablica partycji)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Błąd odczytu z pamięci masowej.&lt;br&gt;Karta SD może być uszkodzona</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Weryfikacja zapisu nie powiodła się. Zawartość karty SD różni się od tego, co zostało na niej zapisane.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Dostosowywanie obrazu</translation>
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
        <translation>Błąd partycjonowania: %1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>Błąd uruchamiania fat32format</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>Błąd uruchomienia fat32format: %1</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>Błąd określania nowej litery dysku</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>Nieprawidłowe urządzenie: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Błąd formatowania (przez udisks2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatowanie nie zostało zaimplementowane dla tej platformy</translation>
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
        <translation type="unfinished">Dysk</translation>
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
        <translation type="unfinished">Zamontowany jako %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[ZABEZPIECZONY PRZED ZAPISEM]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">arta SD jest zabezpieczona przed zapisem.&lt;br&gt;Przesuń przełącznik blokady po lewej stronie karty do góry i spróbuj ponownie.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">WYBIERZ MODEL</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Model Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Pojemność pamięci nie jest wystarczająco duża.&lt;br&gt;Musi wynosić co najmniej %1 GB.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Plik wejściowy nie jest prawidłowym obrazem dysku.&lt;br&gt;Rozmiar pliku %1 bajtów nie jest wielokrotnością 512 bajtów.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Pobieranie i zapisywanie obrazu</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Wybierz obraz</translation>
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
        <translation>Czy chcesz wstępnie wypełnić hasło Wi-Fi z pęku kluczy systemu?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>otwieranie pliku obrazu</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Błąd podczas otwierania pliku obrazu</translation>
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
        <translation>TAK</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>KONTYNUUJ</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>ZAMKNIJ</translation>
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
        <translation type="unfinished">System Operacyjny</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Cofnij</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Cofnij do głównego menu</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Wydany: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">W pamięci cache komputera</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Plik lokalny</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Online - Pobrano %1 GB</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Najpierw podłącz pamięć USB zawierającą obrazy.&lt;br&gt;Obrazy muszą znajdować się w folderze głównym pamięci USB.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">ustaw hostname:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Ustaw login i hasło</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Login:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Hasło:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Skonfiguruj sieć Wi-Fi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">Ukryte SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Kraj Wi-Fi:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Ustawienia lokalizacji</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Strefa czasowa:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Układ klawiatury:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Odegraj dźwięk po zakończeniu</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Wysuń nośnik po zakończeniu</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Włącz telemetrię</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Konfiguracja systemu</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Ogólne</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Usługi</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Opcje</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>ZAPISZ</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">Włącz SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Używaj uwierzytelniania hasłem</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Pozwól tylko na uwierzytelnianie kluczem publiczym</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Ustaw authorized_keys dla &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">Uruchom SSH-KEYGEN</translation>
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
        <translation>Czy chcesz zastosować ustawienia personalizacji systemu operacyjnego?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NIE</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NIE, WYCZYŚĆ USTAWIENIA</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>TAK</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>EDYTUJ USTAWIENIA</translation>
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
        <translation>Model Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Wybierz ten przycisk, aby wybrać docelowy model Raspberry Pi</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>System Operacyjny</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>WYBIERZ OS</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Wybierz ten przycisk, aby zmienić system operacyjny</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Dysk</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>WYBIERZ DYSK</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Wybierz ten przycisk, aby zmienić docelowe urządzenie pamięci masowej</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>ANULUJ ZAPIS</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Anulowanie...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>ANULUJ WERYFIKACJE</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Finalizowanie...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Kontynuuj</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Wybierz ten przycisk, aby rozpocząć zapisywanie obrazu</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Używanie niestandardowego repozytorium: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Nawigacja za pomocą klawiatury: &lt;tab&gt; przejdź do następnego przycisku &lt;spacja&gt; naciśnij przycisk/wybierz element &lt;strzałka w górę/w dół&gt; przejdź w górę/w dół na listach</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Język: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Klawiatura: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Czy na pewno chcesz zakończyć?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager jest wciąż zajęty.&lt;br&gt;Czy na pewno chcesz zakończyć??</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Uwaga</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Przygotowanie do zapisu...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Wszystkie istniejące dane na &apos;%1&apos; zostaną usunięte.&lt;br&gt;Czy na pewno chcesz kontynuować?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Dostępna aktualizacja</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Dostępna jest nowsza wersja Raspberry Pi Imager.&lt;br&gt;Czy chcesz otworzyć stronę, aby go ściągnąć?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Zapisywanie... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Weryfikacja... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Przygotowanie do zapisu... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Błąd</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Zapis zakończony sukcesem</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Usuń</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; został skasowany&lt;br&gt;&lt;br&gt; Możesz teraz wyjąć kartę SD z czytnika.</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; został zapisany na &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt; Możesz teraz wyjąć kartę SD z czytnika.</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Sformatuj kartę jako FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Użyj innego obrazu</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Wybierz plik .img z komputera</translation>
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
