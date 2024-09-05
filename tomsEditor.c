/**** INCLUDES ****/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#include <limits.h>

/**** DEFINES ****/

#define _GNU_SOURCE

#define CTRL_KEY(k) ((k) & 0x1f)
#define HEADER_MESSAGE "TOMS EDITOR WELCOME!"

#define STATUS_MESSAGE_LIFE_TIME 5

#define HEADER_SIZE 1 // this is the number of rows the header takes up at the top of the screen
#define LINE_START_SIZE 2 //this describes how many charicters there are before the line is written! stuff like line numbers ect

#define TAB_SIZE 8 

/**** DATA ****/

typedef struct EditorRow {
    int length;
    char* chars;
    
    int rawLength;
    char* rawChars;
} EditorRow;

struct EditorConfig {
    int cx, cy; //this is for cursor location
    int screenRows;
    int screenCols;
    int numberOfRows; //number of rows in the current file
    int yScroll;
    int xScroll;
    EditorRow* rows;
    int fileModified;
    struct termios orig_termios;
    
    char* filePath;
    size_t filePathLength;
    
    char statusMsg[80];
  	time_t statusMsgTime;
};

struct EditorConfig E;

enum editorKey {
	BACKSPACE = 127,
    ARROW_UP    = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    CTRL_ARROW_LEFT,
    CTRL_ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    DELETE_KEY,
    END
};

/**** PROTOTYPES ****/
void editorSetStatusMessage (const char* fmt, ...);

/**** TERMINAL ****/

//prints out message and ends program withh error when called
void die (const char* string) {
    //clear the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    //puts curse in top left of screen
    write(STDOUT_FILENO, "\x1b[1;1H", 6);
    
    perror(string);
    exit(1);
}

/*
debug function used to write to a debug file, this file is cleard at the when the function is first called during the running
all debug messages get put in a dile called DebugOutput.txt 
*/
int openedDebugFile = 0;
void debugOutput (char* msg) {
    FILE* fp;
    
	if (!openedDebugFile) {
	    fp = fopen("DebugOutput.txt", "w");
	    openedDebugFile++;
	} else {	
    	fp = fopen("DebugOutput.txt", "a");
    }
    fprintf(fp,  "%s\n", msg);
    fclose(fp);
}

void debugOutputChar (char chr) {
    FILE* fp;
    
	if (!openedDebugFile) {
	    fp = fopen("DebugOutput.txt", "w");
	    openedDebugFile++;
	} else {	
    	fp = fopen("DebugOutput.txt", "a");
    }
    fprintf(fp,  "%c\n", chr);
    fclose(fp);
}

void debugOutputInt (int num) {
    FILE* fp;
    
	if (!openedDebugFile) {
	    fp = fopen("DebugOutput.txt", "w");
	    openedDebugFile++;
	} else {	
    	fp = fopen("DebugOutput.txt", "a");
    }
    fprintf(fp,  "%d\n", num);
    fclose(fp);
}

/*
 * This puts the terminal into Raw mode, this is a vertion of th terminal that
 * allows for continues inputs to be prcessed without the need of pressing 
 * 'ENTER' for every line 
 * further more featurs such as echoing keys will be turned off
 */
void enableRawMode () {
    struct termios raw;
    raw = E.orig_termios;

    /* 
     * ECHO shows the keys you type (we turn this off)
     * ICANON allows prgram to be proceeses byte by btye instead of line by line 
     * ISIG stops ctrl-Z/C from stopping the prgram
     * IEXTEN stops ctrl-V
     */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); //c_lflag is for local mode of the terminal
    /*
     * IXON dissables controle frow (ctrl-S/Q)
     * ICRNL stops terminal turning crtl-M into value 10 (as it tyrbs \r into \n)
     * BRKINT if turnd on break condtions cause a SIGINT signal to be sent to teminal 
     * INPCK enables parrity checking (not common in moder terminals)
     * ISTRIP causes 8th byte to be stripped 
     */
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK );
    /*
     * OPOST terminal turns \n into \r\n this will dissable this feature
     */
    raw.c_oflag &= ~(OPOST);
    /*
     * CS8 sets charicter sze to 8 bits per byte, is nor a flag but a bit mask
     */
    raw.c_cflag |= (CS8);

    /*
     * c_cc is an array of controle charicters for varius terminal settings
     * VMIN is the minumum number of bytes needed to read in
     * VTIME is how long to wait untill the next read in (is in tenths of a second)
     */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    //TCSAFLUSH means that the terminal waits for all prending argumtes 
    //to be procesed before the new settings are applyed
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    };
}

void dissableRawMode () {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetarrt");
    }
}

int editorKeyRead () {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) { die("read"); }; 
    }
    
    if (c == '\x1b') { //for escape codes such as arrow keys
        char seq[5]; //need to get the next sequence charcters to kow what has been inputted     

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    
        if (seq[0] == '[') {
        
            if (seq[1] >= '0' && seq[1] <= '9'){

                if (read(STDIN_FILENO, &seq[2], 1) != 1) { return '\x1b'; }

                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': return DELETE_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
                
                if (seq[2] == ';'){
                	
                	if (read(STDIN_FILENO, &seq[3], 1) != 1) { return '\x1b'; }
                	if (read(STDIN_FILENO, &seq[4], 1) != 1) { return '\x1b'; }

		            switch (seq[4]) {   
		                case 'C': return CTRL_ARROW_RIGHT;
		            	case 'D': return CTRL_ARROW_LEFT;       		
		            }
                }
            }
			
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'F': return END;
            }
            
        }

        return '\x1b'; 

    }

    return c;
}

int getCursorPosition (int* row, int* col) {
    char buff[32];
    unsigned int i = 0;
    
    //this requests the cursor position to be written to the screen
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }
    
    while (i < sizeof(buff)) {
        if (read(STDIN_FILENO, &buff[i], 1) != 1) { break; }
        if (buff[i] == 'R') { break; }
        i++;
    }
    
    buff[i] = '\0';

    if (buff[0] != '\x1b' || buff[1] != '[') { return -1; };
    if (sscanf(&buff[2], "%d;%d", row, col) != 2) { return -1; };
    
    return 0;
}

// this gets the poistion not in screen space but in the raw line in the file, so this will compensate for tabs ect
int getCursorPositionInRawFileLine () { 
	int posInRender = E.cx - LINE_START_SIZE;
	int line = E.cy - HEADER_SIZE;
	int i = 0;
	int total = 0;
	EditorRow* row = &E.rows[line];
	
	while (i < row->rawLength && total < posInRender) {
		total = (row->chars[i] == '\t') ? total + TAB_SIZE : total + 1;
		i++;
	}
	 
	return i;
}

int getCursorPositionInRenderdFileLine () { 
	return E.cx - LINE_START_SIZE;
}

/**** APPEND BUFFER ****/
/* this create a buffer to write into for the screen, then the screen is written
 * (using write) to STDOUT_FILENO in one go (instead of useing write statment 
 * many times
 */

struct abuf {
    char* buffer;
    int length;
};

#define ABUF_INIT {NULL, 0}

void abufAppend (struct abuf* buff, char* string, int length) {
    char* new = realloc(buff->buffer, buff->length + length);

    if (new == NULL) { return; }
    memcpy(&new[buff->length], string, length);
    buff->buffer = new;
    buff->length += length;
}

void abufFree (struct abuf* buf) {
    free(buf->buffer);
}

/**** EDITOR OPPERATIOS ****/

void editorFreeRow(EditorRow *row) {
  free(row->chars);
  free(row->rawChars);
}

void editorUpdateRow(EditorRow *row) { //used to update the lines that are actually being maniputalted and rendered on screen
	int j;
	int idx  = 0;
	int tabs = 0;

 	for (j = 0; j < row->rawLength; j++) {
    	if (row->rawChars[j] == '\t') tabs++;
	}

	free(row->chars);
	row->chars = malloc(row->rawLength + tabs*(TAB_SIZE -1) + 1); //1 is subtracted from tab size as row length allready includes that 1!

	for (j = 0; j < row->rawLength; j++) {
		if (row->rawChars[j] == '\t') {
		    row->chars[idx++] = ' ';
      		while (idx % 8 != 0) row->chars[idx++] = ' ';
		} else { 
			row->chars[idx++] = row->rawChars[j];
		}
	}
	
	row->chars[idx] = '\0';
	row->length = idx;
}

void editorInsertRow (int at, char* str, size_t length) {
	if (at < 0 || at > E.numberOfRows) { return; }

	E.rows = realloc(E.rows, sizeof(EditorRow) * (E.numberOfRows + 1));
	memmove(&E.rows[at + 1], &E.rows[at], sizeof(EditorRow) * (E.numberOfRows - at));

	E.rows = realloc(E.rows, sizeof(EditorRow) * (E.numberOfRows + 1));
	
	E.rows[at].rawLength = length;
	E.rows[at].rawChars = malloc(length + 1);
	memcpy(E.rows[at].rawChars, str, length);
	E.rows[at].chars = '\0';
	
	E.rows[at].length = 0;
	E.rows[at].chars = NULL;
	
	editorUpdateRow(&E.rows[at]);
	 
	E.numberOfRows++;

    debugOutput(E.rows[at].chars);
    debugOutput(E.rows[at].rawChars);
}

void editorInsertNewLine () {
	int line = E.cy - HEADER_SIZE;
	int at = getCursorPositionInRenderdFileLine(); //where the break in the line is 
	if (at == 0) {
		editorInsertRow(line, "", 0);
	} else {
	  EditorRow *row = &E.rows[line];
	  editorInsertRow(line + 1, &row->rawChars[at], row->rawLength - at);
	  
	  row = &E.rows[line];
	  row->rawLength = row->rawLength - (row->rawLength - at);
	  row->rawChars[row->rawLength] = '\0';
	  editorUpdateRow(row);
	}

	E.cy++;
	E.cx = LINE_START_SIZE;
}

void editorRowAppendString (EditorRow* row, char* str, size_t length) {

  row->rawChars = realloc(row->rawChars, row->rawLength + length + 1);
  memcpy(&row->rawChars[row->rawLength], str, length);
  row->rawLength += length;
  row->rawChars[row->rawLength] = '\0';
  E.fileModified++;
  editorUpdateRow(row);
}

void editorDelRow(int rowIndex) {
	if (rowIndex < 0 || rowIndex >= E.numberOfRows) { return; }
	
	editorFreeRow(&E.rows[rowIndex]);
	memmove(&E.rows[rowIndex], &E.rows[rowIndex + 1], sizeof(EditorRow) * (E.numberOfRows - rowIndex - 1));
	E.numberOfRows--;
	//E.rows = realloc(E.rows, E.numberOfRows );
}

/*
	"row" is a referance to the row that is being changed
	"at" is where the char is to be inserted
	"c" is the charictor to be insterted
*/
void editorRowInsertChar (EditorRow* row, int at, int c) {
	if (at < 0 || at > row->rawLength) { at = row->rawLength; }
	  
	row->rawChars = realloc(row->rawChars, row->rawLength + 2);
	memmove(&row->rawChars[at + 1], &row->rawChars[at], row->rawLength - at + 1);
	row->rawChars[at] = c;
	row->rawLength++;
	
	editorUpdateRow(row);
}

void editorInsertChar (char c) {
	int line = E.cy - HEADER_SIZE;
	if (line == E.numberOfRows) {
		editorInsertRow(E.numberOfRows ,"", 0);
	} 
	else if (line > E.numberOfRows) { return; }
	else if (line < 0) { return; } 
	editorRowInsertChar(&E.rows[line], getCursorPositionInRawFileLine(), c);
	if (E.cx <= E.rows[line].length) { E.cx++; }
	
	E.fileModified++;
}

void editorRowDelChar(EditorRow* row, int at) {
	if (at < 0 || at >= row->length) { return; }
	memmove(&row->rawChars[at], &row->rawChars[at + 1], row->rawLength - at);
	row->rawLength--;
	row->rawChars = realloc(row->rawChars, row->rawLength);
	
	editorUpdateRow(row);
}

void editorDeleteChar () {
	int line = E.cy - HEADER_SIZE;
	if (line >= E.numberOfRows || line < 0) {
		return;
	} 
	else if (getCursorPositionInRenderdFileLine() < 0) {
	    int newCx = E.rows[E.cy - 1 - HEADER_SIZE].length + LINE_START_SIZE - 1;
	    
    	editorRowAppendString(&E.rows[line - 1], E.rows[line].chars, E.rows[line].length);
    	editorDelRow(E.cy - HEADER_SIZE);
    	E.cy--;
    	E.cx = newCx;
    	
    	
    } else {
		editorRowDelChar(&E.rows[line], getCursorPositionInRawFileLine());
		E.fileModified++;
		E.cx--;
	}
}

/**** OUTPUTS ****/

void editorSetStatusMessage (const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusMsg, sizeof(E.statusMsg), fmt, ap);
	va_end(ap);
	E.statusMsgTime = time(NULL);
}

/*
This is the function that is used to draw the editor row by row onto the screen!
*/
void editorDrawRows (struct abuf* buff) { 
    int y;
    int headerPadding;
    char header[80];
    int headerLen = snprintf(header, sizeof(header), HEADER_MESSAGE);

	// this is drawing the header
	{
		int i = 0;
		
		if (headerLen > E.screenCols) { headerLen = E.screenCols; }
		headerPadding = (E.screenCols - headerLen) / 2;
		    
		abufAppend(buff, "\x1b[K", 3); //clear lines as they are re-drawn
		abufAppend(buff, "\x1b[7m", 4);
		abufAppend(buff, "\x1b[1m", 4);
		abufAppend(buff, "~", 1);
		headerPadding--;
		
		i = headerPadding;
		while (i--) { abufAppend(buff, " ", 1); }

		abufAppend(buff, header, headerLen);
		
		i = headerPadding + 1;
		while (i--) { abufAppend(buff, " ", 1); }
		
		abufAppend(buff, "\x1b[0m", 4);
		abufAppend(buff, "\r\n", 2);
    }
    
    for (y = 0; y < E.screenRows - HEADER_SIZE - 1; y++) { // this -1 is for the status bar at the bottom of the page 
    	int lineNumber = y + E.yScroll;
    	
    	abufAppend(buff, "\x1b[K", 3); //clear lines as they are re-drawn
		abufAppend(buff, "~ ", LINE_START_SIZE);
  		  
        if (lineNumber < E.numberOfRows) {    
            int len = E.rows[lineNumber].length - E.xScroll;
            if (len > E.screenCols) { len = E.screenCols - LINE_START_SIZE; } //compoensate for the start of the line e.g. line numbers
            
            if (len > 0) {
            	abufAppend(buff, &E.rows[lineNumber].chars[E.xScroll], len);
        	}
        	
        	
        }
        
        abufAppend(buff, "\r\n", 2);
    }
    
    {
		//drawing status bar
		int tempStrLen;
		int len;
		
		char tempStr[80];
	
		abufAppend(buff, "\x1b[K", 3); //clear lines as they are re-drawn
		abufAppend(buff, "\x1b[7m", 4);
		abufAppend(buff, "\x1b[1m", 4);
		len = 0;
		
		abufAppend(buff, " FILE PATH: ", 12);
		len += 12;
		abufAppend(buff, E.filePath, E.filePathLength);
		len += E.filePathLength;
		
		abufAppend(buff, " LINE NUMBER: ", 14);
		len += 14;
	  	tempStrLen = snprintf(tempStr, sizeof(tempStr), "%d/%d", E.cy - 1 + E.yScroll, E.numberOfRows);
	  	abufAppend(buff, tempStr, tempStrLen);
	  	len += tempStrLen;
	  	
	  	if (time(NULL) - E.statusMsgTime > STATUS_MESSAGE_LIFE_TIME) { editorSetStatusMessage("N/A"); }
		
		abufAppend(buff, " STATUS MESSAGE: ", 17);
	  	len += 17;
	  	tempStrLen = snprintf(tempStr, sizeof(tempStr), "%s", E.statusMsg);
	  	abufAppend(buff, tempStr, tempStrLen);
	  	len += tempStrLen;
		
		while (len < E.screenCols) {
			abufAppend(buff, " ", 1);
			len++;
	  	}
	 	abufAppend(buff, "\x1b[m", 3);
	}
}

int getCurrentLine () {
	return E.cy + E.yScroll - HEADER_SIZE; //header size needs to be subtracted to get the line yu are on
}

char getCurrentSelctedChar () {
	if (E.cx - LINE_START_SIZE > E.rows[getCurrentLine()].length) { return 0; }
	return E.rows[getCurrentLine()].chars[getCursorPositionInRawFileLine()];
}

void editorRefreshScreen () {
	char cbuff[32];

    struct abuf buff = ABUF_INIT;

    //hide cursor to stop flickering 
    abufAppend(&buff, "\x1b[?25l", 6);
    //puts curse in top left of screen
    abufAppend(&buff, "\x1b[1;1H", 6);

    editorDrawRows(&buff);

    //puts curse in correct location
    
    snprintf(cbuff, sizeof(cbuff), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abufAppend(&buff, cbuff, strlen(cbuff));
    
    abufAppend(&buff, "\x1b[?25h", 6); //show cursor
    
    write(STDOUT_FILENO, buff.buffer, buff.length);
    abufFree(&buff);
}

/**** FILE IO ****/
/*
#define MAX_LINE_LENGTH 150 //this is a masive overestimate so will need to be alterd at some point
char* getNextLine (size_t* length, FILE* file) {
 
}   
*/

void editorOpen (char* filePath) {
	FILE *fp = fopen(filePath, "r");
	
	char *line = NULL;
	size_t lineCap = 0;
	ssize_t lineLen = 0;
	
	free(E.filePath);
	E.filePath = strdup(filePath);
	E.filePathLength = strlen(E.filePath);
	
	if (!fp) { die("fopen"); }

	lineLen = getline(&line, &lineCap, fp);

	while (lineLen != -1) {
		while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
			lineLen--;
		}
			
		editorInsertRow(E.numberOfRows, line, lineLen);	
		lineLen = getline(&line, &lineCap, fp);
	}
	
	free(line);
	fclose(fp);
}

//caller should free return value
char* editorRowsToString (int* bufLength) {
	int totalLength = 0;
	int i;
	
	for (i = 0; i < E.numberOfRows; i++) {
		totalLength += E.rows[i].length + 1; // "+ 1" is for the new line char	
	}
	
	*bufLength = totalLength;
	
	char* buf = malloc(totalLength);
	char* p = buf; // this is a pointer to where the next line will be added 
	for (i = 0; i < E.numberOfRows; i++) {
		memcpy(p, E.rows[i].chars, E.rows[i].length);
		p += E.rows[i].length;
		*p = '\n';
		p++;
		
	}
		
	return buf;
}

void editorSave () {
	if (E.filePath == NULL) return;
	int len;
	char *buf = editorRowsToString(&len);
	/*
	0644 is the standard permissions you usually want for text files. 
	It gives the owner of the file permission to read and write the file, 
	and everyone else only gets permission to read the file.
	*/
	int fd = open(E.filePath, O_RDWR | O_CREAT, 0644); 
	
	if (fd != -1) {  
	
		if (ftruncate(fd, len) != -1) { //creates file to certain size
			write(fd, buf, len);
			close(fd);
			editorSetStatusMessage("Saved file");
			E.fileModified = 0;
		}
	}
	
	free(buf);
}

/**** INPUTS ****/
void scrollScreenY (int offset) {
	E.yScroll += offset;
	if (E.yScroll < 0				) { E.yScroll = 0; }
	if (E.yScroll > E.numberOfRows) { E.yScroll = E.numberOfRows; }
}

void scrollScreenX (int offset) {
	E.xScroll += offset;
	if (E.xScroll < 0			  ) { E.xScroll = 0; }
	if (E.xScroll > E.screenCols) { E.xScroll = E.screenCols; }
}

void editorMoveCursor(int key) {
	int lineLength = E.rows[getCurrentLine()].length;

    switch (key) {
        case ARROW_LEFT:
            E.cx--;
            break;
        case ARROW_DOWN:
            E.cy++;
            break;
        case ARROW_UP:
            E.cy--;
            break;
        case ARROW_RIGHT:
            E.cx++;
            break;
        case CTRL_ARROW_LEFT:
        	if (E.cx > lineLength) { E.cx = lineLength + LINE_START_SIZE - 1; break; } 
        	while (E.cx > 0) {
        		if (getCurrentSelctedChar() == ' ') { break; }
        		E.cx--;
        	}
        	while (E.cx > 0 && getCurrentSelctedChar() == ' ') {
        		E.cx--;
        	}
        	break;
        case CTRL_ARROW_RIGHT:
        	while (E.cx < lineLength - 1 + LINE_START_SIZE) {
        		if (getCurrentSelctedChar() == ' ') { break; }
        		E.cx++;
        	}
        	while (E.cx < lineLength - 1 + LINE_START_SIZE && getCurrentSelctedChar() == ' ') {
        		E.cx++;
        	}
        	break;
        case END:
        	E.cx = lineLength - 1 + LINE_START_SIZE;
        	return;
    }

    if (E.cx < 0) { 
    	E.cx = 0;
    	scrollScreenX(-1);
    }
    if (E.cy < 0) { 
    	E.cy = 0;
    	scrollScreenY(-1);
    }
    
    if (E.cx > E.screenCols) {
    	E.cx = E.screenCols;
    	scrollScreenX(1);
    }
    if (E.cy > E.screenRows) { 
    	E.cy = E.screenRows;
    	scrollScreenY(1);
    }
}

#define QUIT_ATTEMPTS 3
void editorProcessKeypress () {
	static short int quitAttempts = QUIT_ATTEMPTS;  
    int c = editorKeyRead();
    
    switch (c) {
    	case '\r': //enter key
    		editorInsertNewLine();
    		break;
    		
    	case BACKSPACE:
		case CTRL_KEY('h'):
    		 E.cx--;
		case DELETE_KEY:
			editorDeleteChar();
			E.cx++;
			break;
    
        case CTRL_KEY('q'):
        	if (quitAttempts > 1 && E.fileModified) {
        		quitAttempts--;
        		editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quitAttempts);
        		return;
        	} 
			//clear the screen
			write(STDOUT_FILENO, "\x1b[2J", 4);
			//puts curse in top left of screen
			write(STDOUT_FILENO, "\x1b[1;1H", 6);
			exit(0);
            break;
    
		case CTRL_KEY('s'):
			editorSave();
			break;       
            
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case END:
        case CTRL_ARROW_LEFT:
        case CTRL_ARROW_RIGHT:
            editorMoveCursor(c); 
            break;
        case PAGE_UP:
            scrollScreenY(-1);
            break;
        case PAGE_DOWN:
            scrollScreenY(1);
            break;
            
		case CTRL_KEY('l'):
		case '\x1b':
			break;
         
		default:
			editorInsertChar(c);
			break;
    }
    
    quitAttempts = QUIT_ATTEMPTS;
}

/**** INIT ****/

int getWindowSize (int* rows, int* cols) {
    //moves cursor to the bottom right of screen
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
        return -1;
    }

    if (getCursorPosition(rows, cols) != 0) { return -1; }

    return 0;
}


void initEditor () {
    int rows;
    int cols;

    if (getWindowSize(&rows,&cols) == -1) {
        die("getWindowSize");
    }

    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == 1) {
        die("tcgetattr");
    }

    E.screenRows = rows;
    E.screenCols = cols;

    E.cx = 0;
    E.cy = 0;
    E.numberOfRows = 0;
    E.yScroll      = 0;
    E.xScroll      = 0;
    E.rows     = NULL;
    E.filePath = NULL;
    E.fileModified = 0;
    
    E.statusMsg[0] = '\0';
  	E.statusMsgTime = 0;
   
}

int main (int argc, char* argv[]) {
    initEditor();
	debugOutput("init editor succses");
    enableRawMode();
    debugOutput("enabled raw mode");
    atexit(dissableRawMode);
    
    if (argc < 2) {
        die("No file given so closed program");
    }
        
    editorOpen(argv[1]);
    
    debugOutput("open save editor");
    editorSetStatusMessage("HELP-Ctrl = Q | quit-Ctrl S to");

    /*
    reads 1 byte from the standard input untill there 
    are no ore bytes to read (returns 0 at end of the file)
    Ctrl-D will tell termail to stop raeding file so will end
    pressing q will also leave the program
    */
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    };

    return 0;
}
