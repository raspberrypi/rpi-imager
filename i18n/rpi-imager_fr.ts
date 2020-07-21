<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="fr_FR">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="166"/>
        <source>Error writing to storage</source>
        <translation>Erreur d'écriture dans le stockage</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="188"/>
        <location filename="../downloadextractthread.cpp" line="348"/>
        <source>Error extracting archive: %1</source>
        <translation>Erreur lors de l'extraction de l'archive : %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="234"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Erreur lors du montage de la partition FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="244"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Le système d'exploitation n'a pas monté la partition FAT32</translation>
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
        <location filename="../downloadthread.cpp" line="126"/>
        <source>Error running diskpart: %1</source>
        <translation>Erreur lors de l'exécution de diskpart : %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error removing existing partitions</source>
        <translation>Erreur lors de la suppression des partitions existantes</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="174"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Erreur lors de l'exécution d'authopen pour accéder au périphérique du stockage &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="193"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Impossible d'ouvrir le périphérique de stockage &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="207"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Erreur d'écriture lors du formatage du MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="219"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation>Erreur d'écriture lors de la tentative de formatage de la dernière partie de la carte.
Le stockage pourrait annoncer une mauvaise capacité (contrefaçon possible)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="537"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Erreur d'écriture dans le stockage (lors du formatage)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="543"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Erreur d'écriture dans le stockage (pendant l'exécution de fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="560"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Téléchargement corrompu. La signature ne correspond pas</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="590"/>
        <source>Error writing first block (partition table)</source>
        <translation>Erreur lors de l'écriture du premier bloc (table de partion)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="642"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation>Erreur de lecture du stockage.
La carte SD pourrait être défectueuse.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="661"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>La vérification de l'écriture à échoué. Le contenu de la carte SD est différent de ce qui y a été écrit.</translation>
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
        <translation>Erreur lors de l'execution de fat32format : %1</translation>
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
        <translation>Erreur lors de l'exécution de mkfs.fat : %1</translation>
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
        <translation>La capacité de stockage n'est pas assez grande.
Elle nécessite d'être d'au moins %1 GO</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="176"/>
        <source>Input file is not a valid disk image.
File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Le fichier source n'est pas une image disque valide.
La taille du fichier (d'%1 octets) n'est pas un multiple de 512 octets.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="200"/>
        <source>Downloading and writing image</source>
        <translation>Téléchargement et écriture de l'image</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="403"/>
        <source>Select image</source>
        <translation>Sélectionnez l'image</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="38"/>
        <source>Error opening image file</source>
        <translation>Erreur lors de l'ouverture de l'image disque</translation>
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
        <location filename="../MsgPopup.qml" line="108"/>
        <source>YES</source>
        <translation>OUI</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
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
        <location filename="../main.qml" line="300"/>
        <source>Operating System</source>
        <translation>Système d'exploitation</translation>
    </message>
    <message>
        <location filename="../main.qml" line="103"/>
        <source>CHOOSE OS</source>
        <translation>CHOISISSEZ L'OS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="121"/>
        <source>Select this button to change the operating system</source>
        <translation>Sélectionnez ce bouton pour changer le système d'exploitation</translation>
    </message>
    <message>
        <location filename="../main.qml" line="133"/>
        <location filename="../main.qml" line="575"/>
        <source>SD Card</source>
        <translation>Carte SD</translation>
    </message>
    <message>
        <location filename="../main.qml" line="145"/>
        <location filename="../main.qml" line="859"/>
        <source>CHOOSE SD CARD</source>
        <translation>CHOISISSEZ LA CARTE SD</translation>
    </message>
    <message>
        <location filename="../main.qml" line="159"/>
        <source>Select this button to change the destination SD card</source>
        <translation>Sélectionnez ce bouton pour changer la carte SD de destination</translation>
    </message>
    <message>
        <location filename="../main.qml" line="175"/>
        <source>WRITE</source>
        <translation>ÉCRIRE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>Select this button to start writing the image</source>
        <translation>Sélectionnez ce bouton pour commencer l'écriture de l'image</translation>
    </message>
    <message>
        <location filename="../main.qml" line="219"/>
        <source>CANCEL WRITE</source>
        <translation>ANNULER L'ÉCRITURE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="222"/>
        <location filename="../main.qml" line="801"/>
        <source>Cancelling...</source>
        <translation>Annulation...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="233"/>
        <source>CANCEL VERIFY</source>
        <translation>ANNULER LA VÉRIFICATION</translation>
    </message>
    <message>
        <location filename="../main.qml" line="236"/>
        <location filename="../main.qml" line="824"/>
        <location filename="../main.qml" line="877"/>
        <source>Finalizing...</source>
        <translation>Finalisation...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="373"/>
        <location filename="../main.qml" line="853"/>
        <source>Erase</source>
        <translation>Formatter</translation>
    </message>
    <message>
        <location filename="../main.qml" line="374"/>
        <source>Format card as FAT32</source>
        <translation>Formater la carte SD en FAT32</translation>
    </message>
    <message>
        <location filename="../main.qml" line="381"/>
        <source>Use custom</source>
        <translation>Utiliser image personnalisée</translation>
    </message>
    <message>
        <location filename="../main.qml" line="382"/>
        <source>Select a custom .img from your computer</source>
        <translation>Sélectionnez une image disque personnalisée (.img) depuis votre ordinateur</translation>
    </message>
    <message>
        <location filename="../main.qml" line="405"/>
        <source>Back</source>
        <translation>Retour</translation>
    </message>
    <message>
        <location filename="../main.qml" line="406"/>
        <source>Go back to main menu</source>
        <translation>Retour au menu principal</translation>
    </message>
    <message>
        <location filename="../main.qml" line="463"/>
        <source>Released: %1</source>
        <translation>Sorti le : %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="466"/>
        <source>Cached on your computer</source>
        <translation>Mis en cache sur votre ordinateur</translation>
    </message>
    <message>
        <location filename="../main.qml" line="468"/>
        <source>Local file</source>
        <translation>Fichier local</translation>
    </message>
    <message>
        <location filename="../main.qml" line="470"/>
        <source>Online - %1 GB download</source>
        <translation>En ligne - %1 GO téléchargé</translation>
    </message>
    <message>
        <location filename="../main.qml" line="628"/>
        <location filename="../main.qml" line="676"/>
        <source>Mounted as %1</source>
        <translation>Mounté à %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="719"/>
        <source>Are you sure you want to quit?</source>
        <translation>Êtes-vous sûr de vouloir quitter ?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="720"/>
        <source>Raspberry Pi Imager est en cours.&lt;br&gt;Êtes-vous sûr de vouloir quitter ?</source>
        <translation></translation>
    </message>
    <message>
        <location filename="../main.qml" line="731"/>
        <source>Warning</source>
        <translation>Attention</translation>
    </message>
    <message>
        <location filename="../main.qml" line="737"/>
        <location filename="../main.qml" line="804"/>
        <source>Writing... %1%</source>
        <translation>Écriture... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="750"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Toutes les données sur le stockage &apos;%1&apos; vont être supprimées.&lt;br&gt;Êtes-vous sûr de vouloir continuer ?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="783"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Erreur lors du téléchargement de la liste des systèmes d'exploitation à partir d'Internet</translation>
    </message>
    <message>
        <location filename="../main.qml" line="827"/>
        <source>Verifying... %1%</source>
        <translation>Vérification... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="845"/>
        <source>Error</source>
        <translation>Erreur</translation>
    </message>
    <message>
        <location filename="../main.qml" line="852"/>
        <source>Write Successful</source>
        <translation>Écriture réussie</translation>
    </message>
    <message>
        <location filename="../main.qml" line="854"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; a bien été formaté&lt;br&gt;&lt;br&gt;Vous pouvez retirer la carte SD du lecteur.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="856"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; a bien été écrit sur &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Vous pouvez retirer la carte SD du lecteur.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="884"/>
        <location filename="../main.qml" line="926"/>
        <source>Error parsing os_list.json</source>
        <translation>Erreur de lecture du fichier os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="964"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Connectez en premier une clé USB contenant les images.&lt;br&gt;Les images doivent se trouver dans le dossier racine de la clé USB.</translation>
    </message>
</context>
</TS>
