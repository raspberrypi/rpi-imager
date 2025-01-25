<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ka_GE">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="196"/>
        <location filename="../downloadextractthread.cpp" line="385"/>
        <source>Error extracting archive: %1</source>
        <translation>გაშლის შეცდომა არქივისთვის: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="261"/>
        <source>Error mounting FAT32 partition</source>
        <translation>FAT32 დანაყოფის მიმაგრების შეცდომა</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="281"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>ოპერაციულმა სისტემამ FAT32 დანაყოფი არ მიამაგრა</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="304"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>ვერ გადავედი საქაღალდეში &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error writing to storage</source>
        <translation type="vanished">Fehler beim Schreiben auf den Speicher</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="118"/>
        <source>unmounting drive</source>
        <translation>დისკის ოხსნა</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="138"/>
        <source>opening drive</source>
        <translation>მიმდინარეობს დისკის გახნა</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="166"/>
        <source>Error running diskpart: %1</source>
        <translation>შეცდომა diskpart-ის გაშვებისას: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="187"/>
        <source>Error removing existing partitions</source>
        <translation>არსებული დანაყოფების წაშლის შეცდომა</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="213"/>
        <source>Authentication cancelled</source>
        <translation>ავთენტიკაცია გაუქმდა</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="216"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>authopen-ის გაშვების შეცდომა წვდომის მისაღებად დისკთან &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>Not sure if current macOS has that option (or if it got moved/renamed)</translatorcomment>
        <translation>გადაამოწმეთ, აქვს თუ არა &apos;Rasperry Pi Imager&apos;-ს წვდომა &apos;მოხსნად ტომებთან&apos; კონფიდენციალობის პარამეტრებში (&apos;ფაილები და საქაღალდეებში&apos;, ან მიეცით &apos;სრული წვდომა დისკზე&apos;).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="239"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>ვერ გავხსენი საცავის მოწყობილობა &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="281"/>
        <source>discarding existing data on drive</source>
        <translation>დისკზე არსებული მონაცემების მოცილება</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="301"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>დისკის პირველი და ბოლო მეგაბაიტის განულება</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="307"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>ჩაწერის შეცდომა MBR-ის განულებისას</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="319"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>ჩაწერის შეცდომა ბარათის ბოლო ნაწილის გაუქმებისას.&lt;br&gt;ბარათი, შეიძლება, არასწორ ზომას გადმოგვცემს (შეიძლება, ის ყალბია).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="408"/>
        <source>starting download</source>
        <translation>გადმოწერის დასაწყისი</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="468"/>
        <source>Error downloading: %1</source>
        <translation>გადმოწერის შეცდომა: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="665"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>დისკზე ჩაწერისას წვდომა აკრძალულია.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="670"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>როგორც ჩანს, ჩართულია კონტროლირებადი საქაღალდის წვდომა. დაამატეთ rpi-imager.exe და fat32format.exe დაშვებული პროგრამების სიაში და თავიდან სცადეთ.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="677"/>
        <source>Error writing file to disk</source>
        <translation>ფაილის დისკზე ჩაწერის შეცდომა</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="699"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>გადმოწერილი ფაილი დაზიანებულია. ჰეშები არ ემთხვევა</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="711"/>
        <location filename="../downloadthread.cpp" line="763"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>საცავში ჩაწერის შეცდომა (ბუფერების ჩაწერისას)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="718"/>
        <location filename="../downloadthread.cpp" line="770"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>საცავში ჩაწერის შეცდომა (fsync-ის დროს)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="753"/>
        <source>Error writing first block (partition table)</source>
        <translation>პირველი ბლოკის ჩაწერის შეცდომა (დანაყოფების ცხრილი)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="828"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>საცავიდან წაკითხვის შეცდომა.&lt;br&gt;ალბათ SD ბარათი დაზიანებულია.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="847"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>ჩანაწერის გადამოწმება ჩავარდა. SD ბარათი არ შეიცავს იმ მონაცემებს, რაც ზედ ჩაიწერა.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="900"/>
        <source>Customizing image</source>
        <translation>დისკის ასლის ფაილის მორგება</translation>
    </message>
    <message>
        <source>Waiting for FAT partition to be mounted</source>
        <translation type="vanished">Warten auf das Einbinden der FAT-Partition</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation type="vanished">Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation type="vanished">Das Betriebssystem hat die FAT32-Partition nicht eingebunden.</translation>
    </message>
    <message>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation type="vanished">Modifizieren fehlgeschlagen. Die Datei &apos;%1&apos; existiert nicht.</translation>
    </message>
    <message>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation type="vanished">Fehler beim Erstellen von firstrun.sh auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error writing to config.txt on FAT partition</source>
        <translation type="vanished">Fehler beim Schreiben in config.txt auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation type="vanished">Fehler beim Erstellen der user-data cloudinit Datei auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation type="vanished">Fehler beim Erstellen der network-config cloudinit Datei auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation type="vanished">Fehler beim Schreiben in cmdline.txt auf der FAT-Partition</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>დაყოფის შეცდომა: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>fat32format-ის გაშვების შეცდომა</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>fat32format-ის გაშვების შეცდომა: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>ახალი დისკის ასოს განსაზღვრის შეცდომა</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>არასწორი მოწყობილობა: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>დაფორმატების შეცდომა (udisks2-ით)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>sfdisk-ის გაშვების შეცდომა</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>დაყოფამ არ შექმნა მოსალოდნელი FAT დანაყოფი %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>mkfs.fat-ის გაშვების შეცდომა</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>mkfs.fat-ის გაშვების შეცდომა:%1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>ამ პლატფორმისთვის დაფორმატება განხორციელებული არაა</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="253"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>საცავის მოცულობა საკმარისი არაა.&lt;br&gt;საჭიროა, იყოს სულ ცოტა %1 გბ.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="259"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>შეყვანის ფაილი სწორი დისკის ასლის ფაილი არაა.&lt;br&gt;ფაილის ზომაა %1 ბაიტი და ის 512 ბაიტის ნამრავლს არ წარმოადგენს.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="654"/>
        <source>Downloading and writing image</source>
        <translation>მიმდინარეობს დისკის ასლის ფაილის გადმოწერა და ჩაწერა</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="787"/>
        <source>Select image</source>
        <translation>აირჩიეთ დისკის ასლის ფაილი</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="962"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>დროის სინქრონიზაციის შეცდომა. თავიდან ვცდი 3 წამში</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="974"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>თქვენს Ethernet კომუტატორში STP ჩართულია. IP-ის მიღებას დიდი დრო დასჭირდება.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1185"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>გნებავთ wifi-ის პაროლის ავტომატური შევსება სისტემური ბრელოკიდან?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>დისკის ასლის ფაილის გახსნა</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>დისკის ასლის ფაილის გახსნის შეცდომა</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>არა</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="107"/>
        <source>YES</source>
        <translation>დიახ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="116"/>
        <source>CONTINUE</source>
        <translation>გაგრძელება</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="124"/>
        <source>QUIT</source>
        <translation>გასვლა</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="20"/>
        <source>OS Customization</source>
        <translation>ოს-ის მორგება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="62"/>
        <source>General</source>
        <translation>ზოგადი</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="70"/>
        <source>Services</source>
        <translation>სერვისები</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="73"/>
        <source>Options</source>
        <translation>მორგება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="89"/>
        <source>Set hostname:</source>
        <translation>ჰოსტის სახელის დაყენება:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="112"/>
        <source>Set username and password</source>
        <translation>მომხმარებლის სახელისა და პაროლის დაყენება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="134"/>
        <source>Username:</source>
        <translation>მომხმარებელი:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="151"/>
        <location filename="../OptionsPopup.qml" line="220"/>
        <source>Password:</source>
        <translation>პაროლი:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="187"/>
        <source>Configure wireless LAN</source>
        <translation>უსადენო ქსელის მორგება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="206"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="239"/>
        <source>Show password</source>
        <translation>პაროლის ჩვენება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="245"/>
        <source>Hidden SSID</source>
        <translation>დამალული SSID</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="251"/>
        <source>Wireless LAN country:</source>
        <translation>უსადენო ქსელის ქვეყანა:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="262"/>
        <source>Set locale settings</source>
        <translation>დააყენეთ ლოკალის პარამეტრები</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="272"/>
        <source>Time zone:</source>
        <translation>დროის სარტყელი:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="282"/>
        <source>Keyboard layout:</source>
        <translation>კლავიატურის განლაგება:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="299"/>
        <source>Enable SSH</source>
        <translation>SSH-ის ჩართვა</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="318"/>
        <source>Use password authentication</source>
        <translation>პაროლით ავთენტიკაციის გამოყენება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="328"/>
        <source>Allow public-key authentication only</source>
        <translation>მხოლოდ საჯარო გასაღებით ავთენტიკაციის დაშვება</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="346"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>authorized_keys-ის დაყენება მომხმარებლისთვის &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="358"/>
        <source>RUN SSH-KEYGEN</source>
        <translation>გაუშვით SSH-KEYGEN</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="376"/>
        <source>Play sound when finished</source>
        <translation>ხმის დაკვრა დასრულებისას</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="380"/>
        <source>Eject media when finished</source>
        <translation>მედიის გამოღება დასრულებისას</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="384"/>
        <source>Enable telemetry</source>
        <translation>ტელემეტრიის ჩართვა</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="398"/>
        <source>SAVE</source>
        <translation>შენახვა</translation>
    </message>
    <message>
        <source>OS customization options</source>
        <translation type="vanished">OS Anpassungen Optionen</translation>
    </message>
    <message>
        <source>for this session only</source>
        <translation type="vanished">nur für diese Sitzung</translation>
    </message>
    <message>
        <source>to always use</source>
        <translation type="vanished">immer verwenden</translation>
    </message>
    <message>
        <source>Disable overscan</source>
        <translation type="vanished">Overscan deaktivieren</translation>
    </message>
    <message>
        <source>Set password for &apos;%1&apos; user:</source>
        <translation type="vanished">Passwort für &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Skip first-run wizard</source>
        <translation type="vanished">Einrichtungsassistent überspringen</translation>
    </message>
    <message>
        <source>Persistent settings</source>
        <translation type="vanished">Dauerhafte Einstellungen</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="119"/>
        <source>Internal SD card reader</source>
        <translation>შიდა SD ბარათის წამკითხველი</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="77"/>
        <source>Use OS customization?</source>
        <translation>გნებავთ ოს-ის მორგება?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="92"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>გნებავთ, გამოიყენოთ ოს-ის მორგების პარამეტრები?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="134"/>
        <source>NO</source>
        <translation>არა</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>არა, გაასუფთავე პარამეტრები</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="125"/>
        <source>YES</source>
        <translation>დიახ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="102"/>
        <source>EDIT SETTINGS</source>
        <translation>პარამეტრების ჩასწორება</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="22"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="119"/>
        <location filename="../main.qml" line="481"/>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Pi მოწყობილობა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="131"/>
        <source>CHOOSE DEVICE</source>
        <translation>აირჩიეთ მოწყობილობა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="143"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>აირჩიეთ ეს ღილაკი, რომ აირჩიოთ თქვენი სამიზნე Raspberry Pi</translation>
    </message>
    <message>
        <location filename="../main.qml" line="157"/>
        <location filename="../main.qml" line="584"/>
        <source>Operating System</source>
        <translation>ოპერაციული სისტემა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="168"/>
        <location filename="../main.qml" line="1638"/>
        <source>CHOOSE OS</source>
        <translation>აირჩიეთ ოპერაციული სისტემა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>Select this button to change the operating system</source>
        <translation>აირჩიეთ ეს ღილაკი ოპერაციული სისტემის შესაცვლელად</translation>
    </message>
    <message>
        <location filename="../main.qml" line="194"/>
        <location filename="../main.qml" line="979"/>
        <source>Storage</source>
        <translation>საცავი</translation>
    </message>
    <message>
        <location filename="../main.qml" line="330"/>
        <source>Network not ready yet</source>
        <translation>ქსელი ჯერ მზად არაა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1007"/>
        <source>No storage devices found</source>
        <translation>საცავის მოწყობილობები აღმოჩენილი არაა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="205"/>
        <location filename="../main.qml" line="1317"/>
        <source>CHOOSE STORAGE</source>
        <translation>აირჩიეთ საცავი</translation>
    </message>
    <message>
        <location filename="../main.qml" line="219"/>
        <source>Select this button to change the destination storage device</source>
        <translation>აირჩიეთ ეს ღილაკი სამიზნე საცავი მოწყობილობის შესაცვლელად</translation>
    </message>
    <message>
        <location filename="../main.qml" line="265"/>
        <source>CANCEL WRITE</source>
        <translation>ჩაწერის გაუქმება</translation>
    </message>
    <message>
        <location filename="../main.qml" line="268"/>
        <location filename="../main.qml" line="1240"/>
        <source>Cancelling...</source>
        <translation>გაუქმება…</translation>
    </message>
    <message>
        <location filename="../main.qml" line="280"/>
        <source>CANCEL VERIFY</source>
        <translation>გადამოწმების გაუქმება</translation>
    </message>
    <message>
        <location filename="../main.qml" line="283"/>
        <location filename="../main.qml" line="1263"/>
        <location filename="../main.qml" line="1336"/>
        <source>Finalizing...</source>
        <translation>მიმდინარეობს დასრულება...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="292"/>
        <source>Next</source>
        <translation>შემდეგი</translation>
    </message>
    <message>
        <location filename="../main.qml" line="298"/>
        <source>Select this button to start writing the image</source>
        <translation>აირჩიეთ ეს ღილაკი, რომ დაიწყოთ დისკის ასლის ფაილის ჩაწერა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="320"/>
        <source>Using custom repository: %1</source>
        <translation>ვიყენებ მორგებულ რეპოზიტორიას: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="339"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>ნავიგაცია კლავიატურით: &lt;tab&gt; გადადის შემდეგ ღილაკზე &lt;space&gt; ღილაკის დაჭერა/ელემენტის არჩევა &lt;ისარი მაღლა/დაბლა&gt; სიაში მაღლა ასვლა/დაბლა ჩამოსვლა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="360"/>
        <source>Language: </source>
        <translation>ენა: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="383"/>
        <source>Keyboard: </source>
        <translation>კლავიატურა: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="500"/>
        <source>[ All ]</source>
        <translation>[ ყველა ]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Back</source>
        <translation>უკან</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <source>Go back to main menu</source>
        <translation>მთავარ მენიუზე დაბრუნება</translation>
    </message>
    <message>
        <location filename="../main.qml" line="894"/>
        <source>Released: %1</source>
        <translation>გამოსვლის დრო: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="904"/>
        <source>Cached on your computer</source>
        <translation>დაკეშილია თქვენს კომპიუტერზე</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Local file</source>
        <translation>ლოკალური ფაილი</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>Online - %1 GB download</source>
        <translation>ინტერნეტით - %1 გბ გადმოწერა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1042"/>
        <location filename="../main.qml" line="1094"/>
        <location filename="../main.qml" line="1100"/>
        <source>Mounted as %1</source>
        <translation>მიმაგრებულია, როგორც %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1096"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[დაცულია ჩაწერისგან]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1141"/>
        <source>Are you sure you want to quit?</source>
        <translation>მართლა გნებავთ, გახვიდეთ?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1142"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager ჯერ კიდევ დაკავებულია.&lt;br&gt; მაინც გნებავთ გასვლა?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1153"/>
        <source>Warning</source>
        <translation>გაფრთხილება</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1162"/>
        <source>Preparing to write...</source>
        <translation>ჩასაწერად მომზადება...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1176"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>&apos;%1&apos;-ზე არსებული მონაცემები სულ წაიშლება.&lt;br&gt; მართლა გნებავთ ამისი გაკეთება?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1191"/>
        <source>Update available</source>
        <translation>განახლება ხელმისაწვდომია</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1192"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>ხელმისაწვდომია Imager-ის ახალი ვერსია.&lt;br&gt;გნებავთ, გავხსნა ვებგვერდი, რომ გადმოწეროთ ის?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1243"/>
        <source>Writing... %1%</source>
        <translation>მიმდინარეობს ჩაწერა... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1266"/>
        <source>Verifying... %1%</source>
        <translation>გადამოწმება... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1273"/>
        <source>Preparing to write... (%1)</source>
        <translation>მიმდინარეობს ჩაწერისთვის მომზადება... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1293"/>
        <source>Error</source>
        <translation>შეცდომა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1300"/>
        <source>Write Successful</source>
        <translation>ჩაწერა წარმატებულია</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1301"/>
        <location filename="../imagewriter.cpp" line="596"/>
        <source>Erase</source>
        <translation>წაშლა</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1302"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; წაიშალა&lt;br&gt;&lt;br&gt;ახლა შეგიძლიათ, ამოიღოთ SD ბარათი წამკითხველიდან</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1309"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; ჩაიწერა &lt;b&gt;%2&lt;/b&gt;-ზე&lt;br&gt;&lt;br&gt;ახლა შეგიძლიათ, ამოიღოთ SD ბარათი წამკითხველიდან</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1463"/>
        <source>Error parsing os_list.json</source>
        <translation>os_list.json-ის დამუსავების შეცდომა</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="597"/>
        <source>Format card as FAT32</source>
        <translation>ბარათის დაფორმატება FAT32-ზე</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="603"/>
        <source>Use custom</source>
        <translation>მორგებულის გამოყენება</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="604"/>
        <source>Select a custom .img from your computer</source>
        <translation>მორგებული .img ფაილის არჩევა თქვენი კომპიუტერიდან</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1712"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>ჯერ მიუერთეთ USB ბარათი, რომელიც დისკის ასლის ფაილებს შეიცავს.&lt;br&gt;ეს დისკის ასლის ფაილები USB ბარათის ძირითად საქაღალდეში უნდა იყოს მოთავსებული.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1728"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>SD ბარათი ჩაწერისგან დაცულია.&lt;br&gt;დააჭირეთ ჩაკეტვის გადამრთველს ბარათის მარცხენა მხარეს ზემოთკენ და თავიდან სცადეთ.</translation>
    </message>
    <message>
        <source>WRITE</source>
        <translation type="vanished">SCHREIBEN</translation>
    </message>
    <message>
        <source>Select this button to access advanced settings</source>
        <translation type="vanished">Klicken Sie auf diesen Knopf, um zu den erweiterten Einstellungen zu gelangen.</translation>
    </message>
    <message>
        <source>Pi model:</source>
        <translation type="vanished">Pi Modell:</translation>
    </message>
    <message>
        <source>Error downloading OS list from Internet</source>
        <translation type="vanished">Fehler beim Herunterladen der Betriebssystemsliste aus dem Internet</translation>
    </message>
    <message>
        <source>Select this button to change the destination SD card</source>
        <translation type="vanished">Klicke auf diesen Knopf, um die Ziel-SD-Karte zu ändern</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;</source>
        <translation type="vanished">&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben</translation>
    </message>
</context>
</TS>
