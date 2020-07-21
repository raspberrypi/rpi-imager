<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="fr_FR">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="166"/>
        <source>Error writing to storage</source>
        <translation>Erreur d&apos;écriture dans le stockage</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="188"/>
        <location filename="../downloadextractthread.cpp" line="348"/>
        <source>Error extracting archive: %1</source>
        <translation>Erreur lors de l&apos;extraction de l&apos;archive : %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="234"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Erreur lors du montage de la partition FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="244"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Le système d&apos;exploitation n&apos;a pas monté la partition FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="267"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Erreur lors du changement du répertoire &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="131"/>
        <source>Error running diskpart: %1</source>
        <translation>Erreur lors de l&apos;exécution de diskpart : %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="152"/>
        <source>Error removing existing partitions</source>
        <translation>Erreur lors de la suppression des partitions existantes</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="178"/>
        <source>Authentication cancelled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="181"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Erreur lors de l&apos;exécution d&apos;authopen pour accéder au périphérique du stockage &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="182"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="203"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Impossible d&apos;ouvrir le périphérique de stockage &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Erreur d&apos;écriture lors du formatage du MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="229"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation>Erreur d&apos;écriture lors de la tentative de formatage de la dernière partie de la carte.
Le stockage pourrait annoncer une mauvaise capacité (contrefaçon possible)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="350"/>
        <source>Access denied error while writing file to disk.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="355"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="361"/>
        <source>Error writing file to disk</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="589"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Erreur d&apos;écriture dans le stockage (lors du formatage)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="596"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Erreur d&apos;écriture dans le stockage (pendant l&apos;exécution de fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="577"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Téléchargement corrompu. La signature ne correspond pas</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="630"/>
        <source>Error writing first block (partition table)</source>
        <translation>Erreur lors de l&apos;écriture du premier bloc (table de partion)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation>Erreur de lecture du stockage.
La carte SD pourrait être défectueuse.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="705"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>La vérification de l&apos;écriture à échoué. Le contenu de la carte SD est différent de ce qui y a été écrit.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Erreur de partitionnement : %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Erreur lors du démarrage de fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Erreur lors de l&apos;execution de fat32format : %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Erreur lors de la détermination de la nouvelle lettre du stockage</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Stockage invalide : %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Erreur de formatage (via udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Erreur lors du démarrage de sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="196"/>
        <source>Error starting mkfs.fat</source>
        <translation>Erreur lors du démarrage de mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="206"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Erreur lors de l&apos;exécution de mkfs.fat : %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="213"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatage non implémenté pour cette plateforme</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="170"/>
        <source>Storage capacity is not large enough.
Needs to be at least %1 GB</source>
        <translation>La capacité de stockage n&apos;est pas assez grande.
Elle nécessite d&apos;être d&apos;au moins %1 GO</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="176"/>
        <source>Input file is not a valid disk image.
File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Le fichier source n&apos;est pas une image disque valide.
La taille du fichier (d&apos;%1 octets) n&apos;est pas un multiple de 512 octets.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="200"/>
        <source>Downloading and writing image</source>
        <translation>Téléchargement et écriture de l&apos;image</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="409"/>
        <source>Select image</source>
        <translation>Sélectionnez l&apos;image</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="38"/>
        <source>Error opening image file</source>
        <translation>Erreur lors de l&apos;ouverture de l&apos;image disque</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="96"/>
        <source>NO</source>
        <translation>NON</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>OUI</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="122"/>
        <source>CONTINUE</source>
        <translation>CONTINUER</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>Lecteur de carte SD interne</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="91"/>
        <location filename="../main.qml" line="304"/>
        <source>Operating System</source>
        <translation>Système d&apos;exploitation</translation>
    </message>
    <message>
        <location filename="../main.qml" line="103"/>
        <source>CHOOSE OS</source>
        <translation>CHOISISSEZ L&apos;OS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="121"/>
        <source>Select this button to change the operating system</source>
        <translation>Sélectionnez ce bouton pour changer le système d&apos;exploitation</translation>
    </message>
    <message>
        <location filename="../main.qml" line="134"/>
        <location filename="../main.qml" line="588"/>
        <source>SD Card</source>
        <translation>Carte SD</translation>
    </message>
    <message>
        <location filename="../main.qml" line="146"/>
        <location filename="../main.qml" line="868"/>
        <source>CHOOSE SD CARD</source>
        <translation>CHOISISSEZ LA CARTE SD</translation>
    </message>
    <message>
        <location filename="../main.qml" line="159"/>
        <source>Select this button to change the destination SD card</source>
        <translation>Sélectionnez ce bouton pour changer la carte SD de destination</translation>
    </message>
    <message>
        <location filename="../main.qml" line="176"/>
        <source>WRITE</source>
        <translation>ÉCRIRE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="181"/>
        <source>Select this button to start writing the image</source>
        <translation>Sélectionnez ce bouton pour commencer l&apos;écriture de l&apos;image</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>CANCEL WRITE</source>
        <translation>ANNULER L&apos;ÉCRITURE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="224"/>
        <location filename="../main.qml" line="810"/>
        <source>Cancelling...</source>
        <translation>Annulation...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="236"/>
        <source>CANCEL VERIFY</source>
        <translation>ANNULER LA VÉRIFICATION</translation>
    </message>
    <message>
        <location filename="../main.qml" line="239"/>
        <location filename="../main.qml" line="833"/>
        <location filename="../main.qml" line="886"/>
        <source>Finalizing...</source>
        <translation>Finalisation...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="385"/>
        <location filename="../main.qml" line="862"/>
        <source>Erase</source>
        <translation>Formatter</translation>
    </message>
    <message>
        <location filename="../main.qml" line="386"/>
        <source>Format card as FAT32</source>
        <translation>Formater la carte SD en FAT32</translation>
    </message>
    <message>
        <location filename="../main.qml" line="393"/>
        <source>Use custom</source>
        <translation>Utiliser image personnalisée</translation>
    </message>
    <message>
        <location filename="../main.qml" line="394"/>
        <source>Select a custom .img from your computer</source>
        <translation>Sélectionnez une image disque personnalisée (.img) depuis votre ordinateur</translation>
    </message>
    <message>
        <location filename="../main.qml" line="417"/>
        <source>Back</source>
        <translation>Retour</translation>
    </message>
    <message>
        <location filename="../main.qml" line="418"/>
        <source>Go back to main menu</source>
        <translation>Retour au menu principal</translation>
    </message>
    <message>
        <location filename="../main.qml" line="475"/>
        <source>Released: %1</source>
        <translation>Sorti le : %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="478"/>
        <source>Cached on your computer</source>
        <translation>Mis en cache sur votre ordinateur</translation>
    </message>
    <message>
        <location filename="../main.qml" line="480"/>
        <source>Local file</source>
        <translation>Fichier local</translation>
    </message>
    <message>
        <location filename="../main.qml" line="482"/>
        <source>Online - %1 GB download</source>
        <translation>En ligne - %1 GO téléchargé</translation>
    </message>
    <message>
        <location filename="../main.qml" line="639"/>
        <location filename="../main.qml" line="691"/>
        <location filename="../main.qml" line="697"/>
        <source>Mounted as %1</source>
        <translation>Mounté à %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="693"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="735"/>
        <source>Are you sure you want to quit?</source>
        <translation>Êtes-vous sûr de vouloir quitter ?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="736"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="988"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="747"/>
        <source>Warning</source>
        <translation>Attention</translation>
    </message>
    <message>
        <location filename="../main.qml" line="753"/>
        <location filename="../main.qml" line="813"/>
        <source>Writing... %1%</source>
        <translation>Écriture... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Toutes les données sur le stockage &apos;%1&apos; vont être supprimées.&lt;br&gt;Êtes-vous sûr de vouloir continuer ?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="792"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Erreur lors du téléchargement de la liste des systèmes d&apos;exploitation à partir d&apos;Internet</translation>
    </message>
    <message>
        <location filename="../main.qml" line="836"/>
        <source>Verifying... %1%</source>
        <translation>Vérification... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="854"/>
        <source>Error</source>
        <translation>Erreur</translation>
    </message>
    <message>
        <location filename="../main.qml" line="861"/>
        <source>Write Successful</source>
        <translation>Écriture réussie</translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; a bien été formaté&lt;br&gt;&lt;br&gt;Vous pouvez retirer la carte SD du lecteur.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="865"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; a bien été écrit sur &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Vous pouvez retirer la carte SD du lecteur.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="893"/>
        <location filename="../main.qml" line="935"/>
        <source>Error parsing os_list.json</source>
        <translation>Erreur de lecture du fichier os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="973"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Connectez en premier une clé USB contenant les images.&lt;br&gt;Les images doivent se trouver dans le dossier racine de la clé USB.</translation>
    </message>
</context>
</TS>
