#include "Marlin.h"
#include "cardreader.h"
#include "ultralcd.h"
#include "stepper.h"
#include "temperature.h"
#include "language.h"

#ifdef SDSUPPORT


CardReader::CardReader()
{	
    filesize = 0;
    sdpos = 0;
    sdprinting = false;
    cardOK = false;
    saving = false;
    logging = false;
    autostart_atmillis=0;
    workDirDepth = 0;
    memset(workDirParents, 0, sizeof(workDirParents));

    autostart_stilltocheck=true; //the sd start is delayed, because otherwise the serial cannot answer fast enought to make contact with the hostsoftware.
    lastnr=0;
    //power to SD reader
#if SDPOWER > -1
    SET_OUTPUT(SDPOWER); 
    WRITE(SDPOWER,HIGH);
#endif //SDPOWER

    autostart_atmillis=millis()+5000;
}

char *createFilename(char *buffer,const dir_t &p) //buffer>12characters
{
    char *pos=buffer;
    for (uint8_t i = 0; i < 11; i++) 
    {
        if (p.name[i] == ' ')continue;
        if (i == 8) 
        {
            *pos++='.';
        }
        *pos++=p.name[i];
    }
    *pos++=0;
    return buffer;
}


void  CardReader::lsDive(const char *prepend,SdFile parent)
{
    dir_t p;
    uint8_t cnt=0;

    while (parent.readDir(p, longFilename) > 0)
    {
        if( DIR_IS_SUBDIR(&p) && lsAction!=LS_Count && lsAction!=LS_GetFilename) // hence LS_SerialPrint
        {

            char path[13*2];
            char lfilename[13];
            createFilename(lfilename,p);

            path[0]=0;
            if(strlen(prepend)==0) //avoid leading / if already in prepend
            {
                strcat(path,"/");
            }
            strcat(path,prepend);
            strcat(path,lfilename);
            strcat(path,"/");

            //Serial.print(path);

            SdFile dir;
            if(!dir.open(parent,lfilename, O_READ))
            {
                if(lsAction==LS_SerialPrint)
                {
                    SERIAL_ECHO_START;
                    SERIAL_ECHOLN(MSG_SD_CANT_OPEN_SUBDIR);
                    SERIAL_ECHOLN(lfilename);
                }
            }
            lsDive(path,dir);
            //close done automatically by destructor of SdFile
        }
        else
        {
            if (p.name[0] == DIR_NAME_FREE) break;
            if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.'|| p.name[0] == '_') continue;
            if (longFilename[0] != '\0' &&
                (longFilename[0] == '.' || longFilename[0] == '_')) continue;
            if ( p.name[0] == '.')
            {
                if ( p.name[1] != '.')
                    continue;
            }

            if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;
            filenameIsDir=DIR_IS_SUBDIR(&p);

            if(!filenameIsDir)
            {
                if(p.name[8]!='G') continue;
                if(p.name[9]=='~') continue;
            }
            //if(cnt++!=nr) continue;
            createFilename(filename,p);
            if(lsAction==LS_SerialPrint)
            {
                SERIAL_PROTOCOL(prepend);
                SERIAL_PROTOCOLLN(filename);
            }
            else if(lsAction==LS_Count)
            {
                nrFiles++;
            } 
            else if(lsAction==LS_GetFilename)
            {
                if(cnt==nrFiles)
                    return;
                cnt++;

                //SERIAL_PROTOCOL(prepend);
                //SERIAL_PROTOCOLLN(filename);

            }
        }
    }
}

void CardReader::ls() 
{
    lsAction=LS_SerialPrint;
    if(lsAction==LS_Count)
        nrFiles=0;

    root.rewind();
    lsDive("",root);
}

void CardReader::initsd()
{
    cardOK = false;
    if(root.isOpen())
        root.close();
#ifdef SDSLOW
    if (!card.init(SPI_HALF_SPEED,SDSS))
#else
    if (!card.init(SPI_FULL_SPEED,SDSS))
#endif
    {
        //if (!card.init(SPI_HALF_SPEED,SDSS))
        SERIAL_ECHO_START;
        SERIAL_ECHOLNPGM(MSG_SD_INIT_FAIL);
    }
    else if (!volume.init(&card))
    {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_VOL_INIT_FAIL);
    }
    else if (!root.openRoot(&volume)) 
    {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_OPENROOT_FAIL);
    }
    else 
    {
        cardOK = true;
        SERIAL_ECHO_START;
        SERIAL_ECHOLNPGM(MSG_SD_CARD_OK);
    }
    workDir=root;
    curDir=&root;
    /*
    if(!workDir.openRoot(&volume))
    {
    SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
    }
    */

}

void CardReader::setroot()
{
    /*if(!workDir.openRoot(&volume))
    {
    SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
    }*/
    workDir=root;

    curDir=&workDir;
}
void CardReader::release()
{
    sdprinting = false;
    cardOK = false;
}

void CardReader::startFileprint()
{
    if(cardOK)
    {
        sdprinting = true;
    }
}

void CardReader::pauseSDPrint()
{
    if(sdprinting)
    {
        sdprinting = false;
    }
}


void CardReader::openLogFile(char* name)
{
    logging = true; 
    openFile(name, false); //By zyf
}

void CardReader::openFile(char* name,bool read, uint32_t startPos) //By zyf
{
    if(!cardOK)
        return;
    file.close();
    sdprinting = false;

    SdFile myDir;
    curDir=&root;
    char *fname=name;

    char *dirname_start,*dirname_end;

    if(name[0]=='/')
    {
        dirname_start=strchr(name,'/')+1;
        while(dirname_start>0)
        {
            dirname_end=strchr(dirname_start,'/');
            //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
            //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
            if(dirname_end>0 && dirname_end>dirname_start)
            {
                char subdirname[13];
                strncpy(subdirname, dirname_start, dirname_end-dirname_start);
                subdirname[dirname_end-dirname_start]=0;
                SERIAL_ECHOLN(subdirname);
                if(!myDir.open(curDir,subdirname,O_READ))
                {
                    SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
                    SERIAL_PROTOCOL(subdirname);
                    SERIAL_PROTOCOLLNPGM(".");
                    sdprinting = false;
                    return;
                }
                else
                {
                    //SERIAL_ECHOLN(subdirname);
                }

                curDir=&myDir; 
                dirname_start=dirname_end+1;
            }
            else // the reminder after all /fsa/fdsa/ is the filename
            {
                fname=dirname_start;
                //SERIAL_ECHOLN("remaider");
                //SERIAL_ECHOLN(fname);
                break;
            }

        }

    }
    else //relative path
    {
        curDir=&workDir;
        //SERIAL_PROTOCOL(workDir);
    }

    if(read)
    {
        if (file.open(curDir, fname, O_READ)) 
        {
            filesize = file.fileSize();
            SERIAL_PROTOCOLPGM(MSG_SD_FILE_OPENED);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLPGM(MSG_SD_SIZE);
            SERIAL_PROTOCOLLN(filesize);

            //By Zyf
            #ifdef POWER_FAIL_RECV
            if(startPos > 0){
              //SERIAL_PROTOCOLPGM("Print From ");
              //SERIAL_PROTOCOLLN(startPos);
              sdpos = startPos;
              setIndex(sdpos);
            }else{
              sdpos = 0;
            }
            #else
            sdpos = 0;
            #endif

            SERIAL_PROTOCOLLNPGM(MSG_SD_FILE_SELECTED);
            lcd_setstatus(fname);

            #ifdef POWER_FAIL_RECV
            String strFName = fname;
            writeLastFileName(strFName);
            //writePFRStatus(0.0,0);
            #endif
        }
        else
        {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".");
            sdprinting = false;
        }
    }
    else 
    { //write
        if (!file.open(curDir, fname, O_CREAT | O_APPEND | O_WRITE | O_TRUNC))
        {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".");
        }
        else
        {
            saving = true;
            SERIAL_PROTOCOLPGM(MSG_SD_WRITE_TO_FILE);
            SERIAL_PROTOCOLLN(name);
            lcd_setstatus(fname);
        }
    }  
}

void CardReader::removeFile(char* name)
{
    if(!cardOK)
        return;
    file.close();
    sdprinting = false;


    SdFile myDir;
    curDir=&root;
    char *fname=name;

    char *dirname_start,*dirname_end;
    if(name[0]=='/')
    {
        dirname_start=strchr(name,'/')+1;
        while(dirname_start>0)
        {
            dirname_end=strchr(dirname_start,'/');
            //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
            //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
            if(dirname_end>0 && dirname_end>dirname_start)
            {
                char subdirname[13];
                strncpy(subdirname, dirname_start, dirname_end-dirname_start);
                subdirname[dirname_end-dirname_start]=0;
                SERIAL_ECHOLN(subdirname);
                if(!myDir.open(curDir,subdirname,O_READ))
                {
                    SERIAL_PROTOCOLPGM("open failed, File: ");
                    SERIAL_PROTOCOL(subdirname);
                    SERIAL_PROTOCOLLNPGM(".");
                    return;
                }
                else
                {
                    //SERIAL_ECHOLN("dive ok");
                }

                curDir=&myDir; 
                dirname_start=dirname_end+1;
            }
            else // the reminder after all /fsa/fdsa/ is the filename
            {
                fname=dirname_start;
                //SERIAL_ECHOLN("remaider");
                //SERIAL_ECHOLN(fname);
                break;
            }

        }
    }
    else //relative path
    {
        curDir=&workDir;
    }
    if (file.remove(curDir, fname)) 
    {
        SERIAL_PROTOCOLPGM("File deleted:");
        SERIAL_PROTOCOL(fname);
        sdpos = 0;
    }
    else
    {
        SERIAL_PROTOCOLPGM("Deletion failed, File: ");
        SERIAL_PROTOCOL(fname);
        SERIAL_PROTOCOLLNPGM(".");
    }

}

void CardReader::getStatus()
{
    if(cardOK){
        SERIAL_PROTOCOLPGM(MSG_SD_PRINTING_BYTE);
        SERIAL_PROTOCOL(sdpos);
        SERIAL_PROTOCOLPGM("/");
        SERIAL_PROTOCOLLN(filesize);
    }
    else{
        SERIAL_PROTOCOLLNPGM(MSG_SD_NOT_PRINTING);
    }
}
void CardReader::write_command(char *buf)
{
    char* begin = buf;
    char* npos = 0;
    char* end = buf + strlen(buf) - 1;

    file.writeError = false;
    if((npos = strchr(buf, 'N')) != NULL)
    {
        begin = strchr(npos, ' ') + 1;
        end = strchr(npos, '*') - 1;
    }
    end[1] = '\r';
    end[2] = '\n';
    end[3] = '\0';
    file.write(begin);
    if (file.writeError)
    {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_ERR_WRITE_TO_FILE);
    }
}


void CardReader::checkautostart(bool force)
{
    if(!force)
    {
        if(!autostart_stilltocheck)
            return;
        if(autostart_atmillis<millis())
            return;
    }
    autostart_stilltocheck=false;
    if(!cardOK)
    {
        initsd();
        if(!cardOK) //fail
            return;
    }

    char autoname[30];
    sprintf_P(autoname, PSTR("auto%i.g"), lastnr);
    for(int8_t i=0;i<(int8_t)strlen(autoname);i++)
        autoname[i]=tolower(autoname[i]);
    dir_t p;

    root.rewind();

    bool found=false;
    while (root.readDir(p, NULL) > 0) 
    {
        for(int8_t i=0;i<(int8_t)strlen((char*)p.name);i++)
            p.name[i]=tolower(p.name[i]);
        //Serial.print((char*)p.name);
        //Serial.print(" ");
        //Serial.println(autoname);
        if(p.name[9]!='~') //skip safety copies
            if(strncmp((char*)p.name,autoname,5)==0)
            {
                char cmd[30];

                sprintf_P(cmd, PSTR("M23 %s"), autoname);
                enquecommand(cmd);
                enquecommand_P(PSTR("M24"));
                found=true;
            }
    }
    if(!found)
        lastnr=-1;
    else
        lastnr++;
}

void CardReader::closefile()
{
    file.sync();
    file.close();
    saving = false; 
    logging = false;
}

void CardReader::getfilename(const uint8_t nr)
{
    curDir=&workDir;
    lsAction=LS_GetFilename;
    nrFiles=nr;
    curDir->rewind();
    lsDive("",*curDir);

}

uint16_t CardReader::getnrfilenames()
{
    curDir=&workDir;
    lsAction=LS_Count;
    nrFiles=0;
    curDir->rewind();
    lsDive("",*curDir);
    //SERIAL_ECHOLN(nrFiles);
    return nrFiles;
}

void CardReader::chdir(const char * relpath)
{
    SdFile newfile;
    SdFile *parent=&root;

    if(workDir.isOpen())
        parent=&workDir;

    if(!newfile.open(*parent,relpath, O_READ))
    {
        SERIAL_ECHO_START;
        SERIAL_ECHOPGM(MSG_SD_CANT_ENTER_SUBDIR);
        SERIAL_ECHOLN(relpath);
    }
    else
    {
        if (workDirDepth < MAX_DIR_DEPTH) {
            for (int d = ++workDirDepth; d--;)
                workDirParents[d+1] = workDirParents[d];
            workDirParents[0]=*parent;
        }
        workDir=newfile;
    }
    //SERIAL_ECHOLN(relpath);
}

void CardReader::updir()
{
    if(workDirDepth > 0)
    {
        --workDirDepth;
        workDir = workDirParents[0];
        int d;
        for (int d = 0; d < workDirDepth; d++)
            workDirParents[d] = workDirParents[d+1];
    }
}


void CardReader::printingHasFinished()
{
    st_synchronize();
    quickStop();
    file.close();
    sdprinting = false;
    if(SD_FINISHED_STEPPERRELEASE)
    {
        finishAndDisableSteppers(true);										//By Zyf
        //enquecommand_P(PSTR("M117 Print done..."));		//By Zyf
    }
    autotempShutdown();
}


#ifdef POWER_FAIL_RECV
bool bpfWrited = false;

void CardReader::writeLastFileName(String Value){
    if(!cardOK)
        return;
		bpfWrited = false;

    SdFile tf_file;
    SdFile *parent=&root;
    const char *tff = "PFN.TXT";

    bool bFileExists = false;
    if(tf_file.open(*parent, tff, O_READ)){
        bFileExists = true;
        tf_file.close();
    }

#ifdef ZYF_DEBUG
    ZYF_DEBUG_PRINT_LN(Value);
#endif

    String sContent = "";
    char cAll[150];
    char cContent[15];

    sContent = "";
    sContent += Value;
    sContent.toCharArray(cContent, 15);
    sprintf_P(cAll, PSTR("%s"), cContent);

    const char *arrFileContentNew = cAll;

	#ifdef ZYF_DEBUG
    ZYF_DEBUG_PRINT_LN(arrFileContentNew);
	#endif

    uint8_t O_TF = O_CREAT | O_EXCL | O_WRITE;
    if(bFileExists) O_TF = O_WRITE | O_TRUNC;

    if(tf_file.open(*parent, tff, O_TF)){
        tf_file.write(arrFileContentNew);
        tf_file.close();
    }else{
        //removeFile(tff);
        ZYF_DEBUG_PRINT_LN_MSG("New Value Err ");
    }
}

void CardReader::writePFRStatus(float feedrate, int Status){
    if(!cardOK)
        return;

	if(bpfWrited) 
		return;
        
    SdFile tf_file;
    SdFile *parent=&root;
    const char *tff = "PFR.TXT";
    
    bool bFileExists = false;
    if(tf_file.open(*parent, tff, O_READ)){
        bFileExists = true;
        tf_file.close();
    }
    
    String sContent = "";
    char cAll[150];
    char cContent[15];
    char cLine[15];
    const char *arrFileContentNew;
    
    //ZYF_DEBUG_PRINT(Status);
    //ZYF_DEBUG_PRINT_LN(sdprinting);

    if(Status == -1 && sdprinting){
        uint32_t lFPos = sdpos;
        int iTPos = degTargetHotend(0) + 0.5;
        int iTPos1 = degTargetHotend(1) + 0.5;
        int iFanPos = fanSpeed;
        int iT01 = active_extruder == 0 ? 0:1;
        int iBPos = degTargetBed() + 0.5;    

        float fZPos = current_position[Z_AXIS];
        float fEPos = current_position[E_AXIS];
        float fXPos = current_position[X_AXIS];
        float fYPos = current_position[Y_AXIS];

        float fValue = 0.0;

        ///////////// 0 = file Pos
        sContent = lFPos;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 1 = Temp0 Pos
        sContent = iTPos;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 2 = Temp1 Pos
        sContent = iTPos1;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 3 = Fan Pos
        sContent = iFanPos;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 4 = T0T1
        sContent = iT01;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 5 = Bed Temp
        sContent = iBPos;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 6 = Z Pos
        fValue =  fZPos;
        dtostrf(fValue, 1, 2, cContent);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 7 = E Pos
        fValue =  fEPos;
        dtostrf(fValue, 1, 2, cContent);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        //////////////  8 = X  Pos
        fValue =  fXPos;
        dtostrf(fValue, 1, 2, cContent);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////////  9 = Y Pos
        fValue =  fYPos;
        dtostrf(fValue, 1, 2, cContent);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 10 = dual_x_carriage_mode
        sContent = dual_x_carriage_mode;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////////  11 = duplicate_extruder_x_offset
        fValue =  f_zyf_duplicate_extruder_x_offset;
        dtostrf(fValue, 1, 2, cContent);
        sprintf_P(cLine, PSTR("%s|"), cContent);
        strcat(cAll, cLine);

        ///////////// 12 = feedrate
        sContent = feedrate;
        sContent.toCharArray(cContent, 10);
        sprintf_P(cLine, PSTR("%s"), cContent);
        strcat(cAll, cLine);
        arrFileContentNew = cAll;
    }else{
        arrFileContentNew = "0";
    }    

    //ZYF_DEBUG_PRINT_LN(arrFileContentNew);

    uint8_t O_TF = O_CREAT | O_EXCL | O_WRITE;
    if(bFileExists) O_TF = O_WRITE | O_TRUNC;

    if(tf_file.open(*parent, tff, O_TF)){
		bpfWrited = true;
        tf_file.write(arrFileContentNew);
        tf_file.close();
    }else{
        //removeFile(tff);
        ZYF_DEBUG_PRINT_LN_MSG("New Value Err ");
    }
}

///////////////////split
String CardReader::getSplitValue(String data, char separator, int index){
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length()-1;

    for(int i=0; i<=maxIndex && found<=index; i++){
        if(data.charAt(i)==separator || i==maxIndex){
            found++;
            strIndex[0] = strIndex[1]+1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

String CardReader::isPowerFail(){
    if(!cardOK)
        return "";

    String sRet = "";

    SdFile tf_file;
    SdFile *parent=&root;
    const char *tff = "PFN.TXT";

    //Read File
    if(tf_file.open(*parent, tff, O_READ)){
        int16_t fS = tf_file.fileSize() + 1;
        char buf[255];
        char dim1[] = "\n";
        char *dim = dim1;
        int16_t n = tf_file.fgets(buf, fS, dim);
        String strFileContent = "";

        for( int i = 0; i<fS; i++ ) {
            if(buf[i] != '\0')
                strFileContent += buf[i];
        }

		if(strFileContent != "")
            sRet = strFileContent;
    }
    tf_file.close();
    //ZYF_DEBUG_PRINT_LN(sRet);

    if(sRet != ""){
        tff = "PFR.TXT";
        //Read File
        if(tf_file.open(*parent, tff, O_READ)){
            int16_t fS = tf_file.fileSize() + 1;
            char buf[255];
            char dim1[] = "\n";
            char *dim = dim1;
            int16_t n = tf_file.fgets(buf, fS, dim);
            String strFileContent = "";

            for( int i = 0; i<fS; i++ ) {
                if(buf[i] != '\0')
                    strFileContent += buf[i];
            }

            if(strFileContent != ""){
                //ZYF_DEBUG_PRINT_LN(strFileContent);                    
                uint32_t lFPos = atol(const_cast<char*>(getSplitValue(strFileContent, '|', 0).c_str()));

                String strV6 = getSplitValue(strFileContent, '|', 6);        
                char floatbuf[32]; // make this at least big enough for the whole string
                strV6.toCharArray(floatbuf, sizeof(floatbuf));
                float fZPos = atof(floatbuf);

                if(lFPos < 10 || fZPos < 0.1)
                    sRet = "";

                //ZYF_DEBUG_PRINT_LN(lFPos);
                //ZYF_DEBUG_PRINT_LN(fZPos);
            }
        }else{
            sRet = "";
        }
        tf_file.close();
    }
    //ZYF_DEBUG_PRINT_LN(sRet);

    return sRet;
}

String CardReader::get_PowerFialResume() {
    if(!cardOK)
        return "";

    String sRet = "";

    SdFile tf_file;
    SdFile *parent=&root;
    const char *tff = "PFN.TXT";

    //Read File
    if(tf_file.open(*parent, tff, O_READ)){
        int16_t fS = tf_file.fileSize() + 1;
        char buf[255];
        char dim1[] = "\n";
        char *dim = dim1;
        int16_t n = tf_file.fgets(buf, fS, dim);
        String strFileContent = "";

        for( int i = 0; i<fS; i++ ) {
            if(buf[i] != '\0')
                strFileContent += buf[i];
        }

	    if(strFileContent != ""){
            sRet = strFileContent;
        }
    }
    tf_file.close();

    if(sRet != ""){
        tff = "PFR.TXT";
        //Read File
        if(tf_file.open(*parent, tff, O_READ)){
            int16_t fS = tf_file.fileSize() + 1;
            char buf[255];
            char dim1[] = "\n";
            char *dim = dim1;
            int16_t n = tf_file.fgets(buf, fS, dim);
            String strFileContent = "";

            for( int i = 0; i<fS; i++ ) {
                if(buf[i] != '\0')
                    strFileContent += buf[i];
            }

            if(strFileContent != ""){
                sRet = sRet + "|" + strFileContent;                
            }
        }
        tf_file.close();
    }
    return sRet;
}

#endif //Power fail recv

#endif //SDSUPPORT