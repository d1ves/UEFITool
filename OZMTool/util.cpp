/* util.cpp

Copyright (c) 2014, tuxuser. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <QtEndian>
#include <QDirIterator>
#include <QDateTime>
#include <QUuid>
#include <qtplist/PListParser.h>
#include "../ffs.h"
#include "../peimage.h"
#include "util.h"

/* General stuff */

UINT8 fileOpen(QString path, QByteArray & buf)
{
    QFileInfo fileInfo(path);

    if (!fileInfo.exists())
        return ERR_FILE_NOT_FOUND;

    QFile inputFile;
    inputFile.setFileName(path);

    if (!inputFile.open(QFile::ReadOnly))
        return ERR_FILE_OPEN;

    buf.clear();

    buf.append(inputFile.readAll());
    inputFile.close();

    return ERR_SUCCESS;
}

UINT8 fileWrite(QString path, QByteArray & buf)
{
    QFileInfo fileInfo(path);

    if (fileInfo.exists())
        printf("Warning: File already exists! Overwriting it...\n");

    QFile writeFile;
    writeFile.setFileName(path);

    if (!writeFile.open(QFile::WriteOnly))
        return ERR_FILE_OPEN;

    if(writeFile.write(buf) != buf.size())
        return ERR_FILE_WRITE;

    writeFile.close();

    return ERR_SUCCESS;
}

BOOLEAN fileExists(QString path)
{
    QFileInfo fileInfo = QFileInfo(path);

    return fileInfo.exists();
}

UINT8 dirCreate(QString path)
{
    QDir dir;
    if (dir.cd(path))
        return ERR_DIR_ALREADY_EXIST;

    if (!dir.mkpath(path))
        return ERR_DIR_CREATE;

    return ERR_SUCCESS;
}

BOOLEAN dirExists(QString path)
{
    QDir dir(path);

    return dir.exists();
}

QString pathConcatenate(QString path, QString filename)
{
    return QDir(path).filePath(filename);
}

UINT32 getDateTime()
{
    QDateTime dateTime = QDateTime::currentDateTime();
    return dateTime.toTime_t();
}

UINT16 getUInt16(QByteArray & buf, UINT32 start, bool fromBE)
{
    UINT16 tmp = 0;

    tmp = (tmp << 8) + buf.at(start+0);
    tmp = (tmp << 8) + buf.at(start+1);

    if(fromBE)
        return qFromBigEndian(tmp);
    else
        return tmp;
}

UINT32 getUInt32(QByteArray & buf, UINT32 start, bool fromBE)
{
    UINT32 tmp = 0;

    tmp = (tmp << 8) + buf.at(start+0);
    tmp = (tmp << 8) + buf.at(start+1);
    tmp = (tmp << 8) + buf.at(start+2);
    tmp = (tmp << 8) + buf.at(start+3);

    if(fromBE)
        return qFromBigEndian(tmp);
    else
        return tmp;
}

/* Specific stuff */

UINT8 getGUIDfromFile(QByteArray object, QString & name)
{
    QByteArray header;
    EFI_GUID* guid;
    header = object.left(sizeof(EFI_GUID));
    guid = (EFI_GUID*)(header.constData());

    // Get info
    name = guidToQString(*guid);
    return ERR_SUCCESS;
}

UINT8 plistReadExecName(QByteArray plist, QString & name)
{
    static const QString execIdentifier = "CFBundleExecutable";
    QString plistExec;

    QVariantMap parsed = PListParser::parsePList(plist).toMap();

    if (parsed.contains(execIdentifier))
        plistExec = parsed.value(execIdentifier).toString();

    if(plistExec.isEmpty()) {
        printf("ERROR: '%s'' in plist is blank. Aborting!\n", qPrintable(execIdentifier));
        return ERR_ERROR;
    }

    name = plistExec;

    return ERR_SUCCESS;
}

UINT8 plistReadBundleVersion(QByteArray plist, QString & version)
{
    static const QString versionIdentifier = "CFBundleShortVersionString";
    QString plistVersion;

    QVariantMap parsed = PListParser::parsePList(plist).toMap();

    if (parsed.contains(versionIdentifier))
        plistVersion = parsed.value(versionIdentifier).toString();

    if(plistVersion.isEmpty()) {
        version = "?";
        return ERR_ERROR;
    }

    version = plistVersion;

    return ERR_SUCCESS;
}

UINT8 checkAggressivityLevel(int aggressivity) {
    QString level;

    switch(aggressivity) {
    case RUN_AS_IS:
        level = "Do nothing - Inject as-is";
        break;
    case RUN_DELETE:
         level = "Delete network stuff from BIOS";
         break;
    case RUN_DEL_OZM_NREQ:
         level = "Delete non-required Ozmosis files";
         break;
    default:
        printf("Warning: Invalid aggressivity level set!\n");
        return ERR_ERROR;
    }

    printf("Info: Aggressivity level set to '%s'...\n", qPrintable(level));
    return ERR_SUCCESS;
}

UINT8 convertOzmPlist(QString input, QByteArray & out)
{
    UINT8 ret;
    QByteArray plist;

    ret = fileOpen(input, plist);
    if(ret) {
        printf("ERROR: Open failed: %s\n", qPrintable(input));
        return ERR_ERROR;
    }

    ret = ffsCreate(plist, ozmosisDefaults.GUID, ozmosisDefaults.name, out);
    if(ret) {
        printf("ERROR: KEXT2FFS failed on '%s'\n", qPrintable(ozmDefaultsFilename));
        return ERR_ERROR;
    }

    return ERR_SUCCESS;
}

UINT8 convertKext(QString input, int kextIndex, QString basename, QByteArray & out)
{
    UINT8 ret;
    UINT8 nullterminator = 0;

    QString sectionName, guid;
    QString bundleVersion, execName;
    QDir dir;

    QFileInfo binaryPath;
    QFileInfo plistPath;

    QByteArray plistbuf;
    QByteArray binarybuf;
    QByteArray toConvertBinary;

    // Check all folders in input-dir

    if(kextIndex > 0xF) {
        printf("ERROR: Invalid kextIndex '%i' supplied!\n", kextIndex);
        return ERR_ERROR;
    }

    dir.setPath(input);
    dir = dir.filePath("Contents");
    plistPath.setFile(dir,"Info.plist");
    dir = dir.filePath("MacOS");

    if (!dir.exists()) {
        printf("ERROR: Kext-dir invalid: */Contents/MacOS/ missing!\n");
        return ERR_ERROR;
    }

    if (!plistPath.exists()) {
        printf("ERROR: Kext-dir invalid: */Contents/Info.plist missing!\n");
        return ERR_ERROR;
    }

    ret = fileOpen(plistPath.filePath(), plistbuf);
    if(ret) {
        printf("ERROR: Opening '%s' failed!\n", qPrintable(plistPath.filePath()));
        return ret;
    }

    ret = plistReadExecName(plistbuf, execName);
    if(ret) {
        printf("ERROR: Failed to get executableName Info.plist\n");
        return ret;
    }

    binaryPath.setFile(dir, execName);

    if (!binaryPath.exists()) {
        printf("ERROR: Kext-dir invalid: */Contents/MacOS/%s missing!\n",
               qPrintable(execName));
        return ERR_ERROR;
    }

    ret = fileOpen(binaryPath.filePath(), binarybuf);
    if (ret) {
        printf("ERROR: Opening '%s' failed!\n", qPrintable(binaryPath.filePath()));
        return ERR_ERROR;
    }

    ret = plistReadBundleVersion(plistbuf, bundleVersion);
    if (ret) {
        printf("Info: Unable to get version string...\n");
        sectionName = basename;
    }
    else
        sectionName.sprintf("%s.Rev-%s",qPrintable(basename), qPrintable(bundleVersion));

    guid = kextGUID.arg(kextIndex, 1, 16).toUpper();

    toConvertBinary.append(plistbuf);
    toConvertBinary.append(nullterminator);
    toConvertBinary.append(binarybuf);

    ret = ffsCreate(toConvertBinary, guid, sectionName, out);
    if(ret) {
        printf("ERROR: KEXT2FFS failed on '%s'\n", qPrintable(sectionName));
        return ERR_ERROR;
    }

    return ERR_SUCCESS;
}

UINT8 ffsCreate(QByteArray body, QString guid, QString sectionName, QByteArray & out)
{
    QByteArray bufSectionName;
    QByteArray fileBody, header;

    /* FFS PE32 Section */
    header.fill(0, sizeof(EFI_COMMON_SECTION_HEADER));
    EFI_COMMON_SECTION_HEADER* PE32SectionHeader = (EFI_COMMON_SECTION_HEADER*)header.data();

    uint32ToUint24(sizeof(EFI_COMMON_SECTION_HEADER)+body.size(), PE32SectionHeader->Size);
    PE32SectionHeader->Type = EFI_SECTION_PE32;

    fileBody.append(header, sizeof(EFI_COMMON_SECTION_HEADER));
    fileBody.append(body);

    /* FFS User Interface */
    header.clear();
    header.fill(0, sizeof(EFI_USER_INTERFACE_SECTION));
    EFI_USER_INTERFACE_SECTION* UserInterfaceSection = (EFI_USER_INTERFACE_SECTION*)header.data();

    /* Convert sectionName to unicode data */
    bufSectionName.append((const char*) (sectionName.utf16()), sectionName.size() * 2);

    uint32ToUint24(sizeof(EFI_USER_INTERFACE_SECTION)+bufSectionName.size(), UserInterfaceSection->Size);
    UserInterfaceSection->Type = EFI_SECTION_USER_INTERFACE;

    /* Align for next section */
    UINT8 alignment = fileBody.size() % 4;
    if (alignment) {
        alignment = 4 - alignment;
        fileBody.append(QByteArray(alignment, '\x00'));
    }

    fileBody.append(header, sizeof(EFI_USER_INTERFACE_SECTION));
    fileBody.append(bufSectionName);

    /* FFS File */
    const static UINT8 revision = 0;
    const static UINT8 erasePolarity = 0;
    const static UINT32 size = fileBody.size();

    QUuid uuid = QUuid(guid);

    header.clear();
    header.fill(0, sizeof(EFI_FFS_FILE_HEADER));
    EFI_FFS_FILE_HEADER* fileHeader = (EFI_FFS_FILE_HEADER*)header.data();

    uint32ToUint24(sizeof(EFI_FFS_FILE_HEADER)+size, fileHeader->Size);
    fileHeader->Attributes = 0x00;
    fileHeader->Attributes |= (erasePolarity == ERASE_POLARITY_TRUE) ? '\xFF' : '\x00';
    fileHeader->Type = EFI_FV_FILETYPE_FREEFORM;
    fileHeader->State = EFI_FILE_HEADER_CONSTRUCTION | EFI_FILE_HEADER_VALID | EFI_FILE_DATA_VALID;
    // Invert state bits if erase polarity is true
    if (erasePolarity == ERASE_POLARITY_TRUE)
        fileHeader->State = ~fileHeader->State;

    memcpy(&fileHeader->Name, &uuid.data1, sizeof(EFI_GUID));

    // Calculate header checksum
    fileHeader->IntegrityCheck.Checksum.Header = 0;
    fileHeader->IntegrityCheck.Checksum.File = 0;
    fileHeader->IntegrityCheck.Checksum.Header = calculateChecksum8((UINT8*)fileHeader, sizeof(EFI_FFS_FILE_HEADER)-1);

    // Set data checksum
    if (fileHeader->Attributes & FFS_ATTRIB_CHECKSUM)
        fileHeader->IntegrityCheck.Checksum.File = calculateChecksum8((UINT8*)fileBody.constData(), fileBody.size());
    else if (revision == 1)
        fileHeader->IntegrityCheck.Checksum.File = FFS_FIXED_CHECKSUM;
    else
        fileHeader->IntegrityCheck.Checksum.File = FFS_FIXED_CHECKSUM2;

    out.clear();
    out.append(header, sizeof(EFI_FFS_FILE_HEADER));
    out.append(fileBody);

    return ERR_SUCCESS;
}

UINT8 extractDSDTfromAmiboardInfo(QByteArray amiboardbuf, QByteArray & out)
{
    INT32 offset;
    UINT32 size = 0;
    EFI_IMAGE_DOS_HEADER *HeaderDOS;

    HeaderDOS = (EFI_IMAGE_DOS_HEADER *)amiboardbuf.data();

    if (HeaderDOS->e_magic != EFI_IMAGE_DOS_SIGNATURE) {
        printf("Error: Invalid file, not AmiBoardInfo. Aborting!\n");
        return ERR_INVALID_FILE;
    }

    offset = amiboardbuf.indexOf(DSDT_HEADER);
    if(offset < 0) {
        printf("ERROR: DSDT wasn't found in AmiBoardInfo");
        return ERR_FILE_NOT_FOUND;
    }

    size = getUInt32(amiboardbuf, offset+DSDT_HEADER_SZ, TRUE);

    if(size > (UINT32)(amiboardbuf.size()-offset)) {
        printf("ERROR: Read invalid size from DSDT. Aborting!\n");
        return ERR_INVALID_PARAMETER;
    }

    out.append(amiboardbuf.mid(offset, size));

    return ERR_SUCCESS;
}

UINT8 injectDSDTintoAmiboardInfo(QByteArray amiboardbuf, QByteArray dsdtbuf, QByteArray & out)
{
    int i;
    INT32 offset, diffDSDT;
    UINT32 oldDSDTsize = 0, newDSDTsize;
    EFI_IMAGE_DOS_HEADER *HeaderDOS;
    EFI_IMAGE_NT_HEADERS64 *HeaderNT;
    EFI_IMAGE_SECTION_HEADER *Section;
    QByteArray patchedAmiBoard;

    HeaderDOS = (EFI_IMAGE_DOS_HEADER *)amiboardbuf.data();

    if (HeaderDOS->e_magic != EFI_IMAGE_DOS_SIGNATURE) {
        printf("Error: Invalid file, not AmiBoardInfo. Aborting!\n");
        return ERR_INVALID_FILE;
    }

    offset = amiboardbuf.indexOf(DSDT_HEADER);
    if(offset < 0) {
        printf("ERROR: DSDT wasn't found in AmiBoardInfo");
        return ERR_FILE_NOT_FOUND;
    }

    oldDSDTsize = getUInt32(amiboardbuf, offset+DSDT_HEADER_SZ, TRUE);

    printf("amiboard Sz: %X\n", amiboardbuf.size());
    printf("offset: %X\n", offset);
    printf("oldDSDTSize: %X\n", oldDSDTsize);

    if(oldDSDTsize > (UINT32)(amiboardbuf.size()-offset)) {
        printf("ERROR: Read invalid size from DSDT. Aborting!\n");
        return ERR_INVALID_PARAMETER;
    }

    if(amiboardbuf.indexOf(UNPATCHABLE_SECTION) > 0) {
        printf("ERROR: AmiBoardInfo contains '.ROM' section => unpatchable atm!\n");
        return ERR_ERROR;
    }

    newDSDTsize = dsdtbuf.size();
    diffDSDT = newDSDTsize - oldDSDTsize;

    if(diffDSDT <= 0) {
        printf("Info: New DSDT is not larger than old one, no need to patch anything :)\n");
        QByteArray padbytes;
        padbytes.fill(0, (diffDSDT * (-1))); // negative val -> positive
        out.append(amiboardbuf.left(offset)); // Start of PE32
        out.append(dsdtbuf); // new DSDT
        out.append(padbytes); // padding to match old DSDT location
        out.append(amiboardbuf.mid(offset+oldDSDTsize)); // rest of PE32
        return ERR_SUCCESS;
    }

    HeaderNT = (EFI_IMAGE_NT_HEADERS64 *)amiboardbuf.mid(HeaderDOS->e_lfanew).data();

#if 1
    printf("*** IMAGE_FILE_HEADER ***\n");
    printf(" \
           Characteristics: %X\n \
           Machine: %X\n \
           Num Sections: %i\n \
           Num Symbols: %i\n \
           Ptr SymbolTable: %X\n \
           Sz OptionalHeader: %X\n \
           TimeStamp: %X\n\n",
           HeaderNT->FileHeader.Characteristics,
           HeaderNT->FileHeader.Machine,
           HeaderNT->FileHeader.NumberOfSections,
           HeaderNT->FileHeader.NumberOfSymbols,
           HeaderNT->FileHeader.PointerToSymbolTable,
           HeaderNT->FileHeader.SizeOfOptionalHeader,
           HeaderNT->FileHeader.TimeDateStamp);

    printf("*** IMAGE_OPTIONAL_HEADER64 ***\n");
    printf(" \
          Entrypoint Addr: %X\n \
          Base of Code: %X\n \
          Checksum: %X\n \
          FileAlignment: %X\n \
          ImageBase: %llX\n \
          Magic: %X\n \
          Num RVA and Sizes: %X\n \
          SectionAlignment: %X\n \
          SizeOfCode: %X\n \
          SizeOfHeaders: %X\n \
          SizeOfImage: %X\n \
          SizeOfInitializedData: %X\n \
          SizeOfUninitializedData: %X\n\n",
          HeaderNT->OptionalHeader.AddressOfEntryPoint,
          HeaderNT->OptionalHeader.BaseOfCode,
          HeaderNT->OptionalHeader.CheckSum,
          HeaderNT->OptionalHeader.FileAlignment,
          HeaderNT->OptionalHeader.ImageBase,
          HeaderNT->OptionalHeader.Magic,
          HeaderNT->OptionalHeader.NumberOfRvaAndSizes,
          HeaderNT->OptionalHeader.SectionAlignment,
          HeaderNT->OptionalHeader.SizeOfCode,
          HeaderNT->OptionalHeader.SizeOfHeaders,
          HeaderNT->OptionalHeader.SizeOfImage,
          HeaderNT->OptionalHeader.SizeOfInitializedData,
          HeaderNT->OptionalHeader.SizeOfUninitializedData);

    printf("*** Data Directories ***\n");

    for ( i = 0; i < EFI_IMAGE_NUMBER_OF_DIRECTORY_ENTRIES ;i++) {

        if(HeaderNT->OptionalHeader.DataDirectory[i].VirtualAddress == 0)
            continue;

        printf("DataDirectory %02X\n \
               VirtualAddress: %x\n \
               Size:           %x\n\n",
               i,
               HeaderNT->OptionalHeader.DataDirectory[i].VirtualAddress,
               HeaderNT->OptionalHeader.DataDirectory[i].Size);
    }

    UINT32 sectionsStart = HeaderDOS->e_lfanew+sizeof(EFI_IMAGE_NT_HEADERS64);
    Section = (EFI_IMAGE_SECTION_HEADER *)amiboardbuf.mid(sectionsStart).data();

    printf("*** Sections ***\n");

    for (i = 0 ; i < HeaderNT->FileHeader.NumberOfSections; i++) {
        printf("Section %02X\n \
               Name: %s\n \
               Characteristics: %X\n \
               Num LineNumbers: %X\n \
               Num Relocations: %X\n \
               Ptr LineNumbers: %X\n \
               Ptr RawData:     %X\n \
               Ptr Relocations: %X\n \
               Sz RawData:      %X\n \
               VirtualAddress:  %X\n \
               Misc PhysAddress:%X\n \
               Misc VirtualSize:%X\n",
               i,
               Section[i].Name,
               Section[i].Characteristics,
               Section[i].NumberOfLinenumbers,
               Section[i].NumberOfRelocations,
               Section[i].PointerToLinenumbers,
               Section[i].PointerToRawData,
               Section[i].PointerToRelocations,
               Section[i].SizeOfRawData,
               Section[i].VirtualAddress,
               Section[i].Misc.PhysicalAddress,
               Section[i].Misc.VirtualSize);
    }
#endif


    return ERR_SUCCESS;
}
