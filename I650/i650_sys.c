/* i650_sys.c: IBM 650 Simulator system interface.

   Copyright (c) 2018, Roberto Sancho

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERTO SANCHO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "i650_defs.h"
#include "sim_card.h"
#include <ctype.h>

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char                sim_name[] = "IBM 650";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &cdr_dev,
    &cdp_dev,
//XXX    &mta_dev,
    NULL
};

/* Device addressing words */

DIB  cdr_dib = { 1, &cdr_cmd, NULL };
DIB  cdp_dib = { 3, &cdp_cmd, NULL };
//XXX DIB  mt_dib = { CH_TYP_76XX, NUM_UNITS_MT, 0000, 0000, &mt_cmd, &mt_ini };

/* Simulator stop codes */
const char         *sim_stop_messages[] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Card Read/Punch Error",
    "Programmed Stop",
    "Overflow",
    "Opcode Execution Error",
    "Address Error",
    0
};

static t_stat ibm650_deck_cmd(int32 arg, CONST char *buf);

static CTAB aux_cmds [] = {
/*    Name         Action Routine     Argument   Help String */
/*    ----------   -----------------  ---------  ----------- */
    { "CARDDECK",  &ibm650_deck_cmd,         0,  "Card Deck Operations: Join/Split/Print\n"    },

    { NULL }
    };

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {0, 0}
};

DEBTAB              crd_debug[] = {
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {0, 0}
};

// simulator available IBM 533 wirings
struct card_wirings wirings[] = {
    {WIRING_8WORD,  "8WORD"},
    {WIRING_SOAP,   "SOAP"}, 
    {WIRING_IS,     "IS"}, 
    {WIRING_IT,     "IT"}, 
    {0, 0},
};


// code of char in IBM 650 memory
char    mem_to_ascii[101] = {
/* 00 */  ' ', '~', '~', '~', '~', '~', '~', '~', '~', '~',
/* 10 */  '~', '~', '~', '~', '~', '~', '~', '~', '.', ')',
/* 20 */  '+', '~', '~', '~', '~', '~', '~', '~', '$', '*',
/* 30 */  '-', '/', '~', '~', '~', '~', '~', '~', ',', '(',
/* 40 */  '~', '~', '~', '~', '~', '~', '~', '~', '=', '-',
/* 50 */  '~', '~', '~', '~', '~', '~', '~', '~', '~', '~',
/* 60 */  '~', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
/* 70 */  '~', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
/* 80 */  '~', '~', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
/* 90 */  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
0};

// representation of word digit 0-9 in card including Y(12) and X(11) punchs
char    digits_ascii[31] = {
          '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',   /* 0-9 */  
          '?', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',   /* 0-9 w/HiPunch Y(12) */
          '!', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',   /* 0-9 w/Negative Punch X(11) */
          0};

uint16          ascii_to_hol[128] = {
   /* Control                              */
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,    /*0-37*/
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*  sp      !      "      #      $      %      &      ' */
   /* none   Y28    78     T28    Y38    T48    XT     48  */
    0x000, 0x600, 0x006, 0x282, 0x442, 0x222, 0xA00, 0x022,     /* 40 - 77 */
   /*   (      )      *      +      ,      -      .      / */
   /* T48    X48    Y48    X      T38    T      X38    T1  */
    0x222, 0x822, 0x422, 0x800, 0x242, 0x400, 0x842, 0x300,
   /*   0      1      2      3      4      5      6      7 */
   /* T      1      2      3      4      5      6      7   */
    0x200, 0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
   /*   8      9      :      ;      <      =      >      ? */
   /* 8      9      58     Y68    X68    38     68     X28 */
    0x002, 0x001, 0x012, 0x40A, 0x80A, 0x042, 0x00A, 0x882,
   /*   @      A      B      C      D      E      F      G */
   /*  82    X1     X2     X3     X4     X5     X6     X7  */
    0x022, 0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,     /* 100 - 137 */
   /*   H      I      J      K      L      M      N      O */
   /* X8     X9     Y1     Y2     Y3     Y4     Y5     Y6  */
    0x802, 0x801, 0x500, 0x480, 0x440, 0x420, 0x410, 0x408,
   /*   P      Q      R      S      T      U      V      W */
   /* Y7     Y8     Y9     T2     T3     T4     T5     T6  */
    0x404, 0x402, 0x401, 0x280, 0x240, 0x220, 0x210, 0x208,
   /*   X      Y      Z      [      \      ]      ^      _ */
   /* T7     T8     T9     X58    X68    T58    T78     28 */
    0x204, 0x202, 0x201, 0x812, 0x20A, 0x412, 0x406, 0x082,
   /*   `      a      b      c      d      e      f      g */
    0x212, 0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,     /* 140 - 177 */
   /*   h      i      j      k      l      m      n      o */
    0xA02, 0xA01, 0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08,
   /*   p      q      r      s      t      u      v      w */
    0xC04, 0xC02, 0xC01, 0x680, 0x640, 0x620, 0x610, 0x608,
   /*   x      y      z      {      |      }      ~    del */
   /*                     Y78     X78    78     79         */
    0x604, 0x602, 0x601, 0x406, 0x806,0x0006,0x0005,0xf000
};


/* Initialize vm  */
void
vm_init(void) {
    int i;
    // Initialize vm memory to all plus zero 
    for(i = 0; i < MAXMEMSIZE; i++) DRUM[i] = DRUM_NegativeZeroFlag[i] = 0;
    // init specific commands
    sim_vm_cmd = aux_cmds;                       /* set up the auxiliary command table */
}


void (*sim_vm_init) (void) = &vm_init;

/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
   /* Currently not implimented until I know format of load files */
    return SCPE_NOFNC;
}

/* Symbol tables */
typedef struct _opcode
{
    uint16              opbase;
    const char         *name;
    uint8               bReadData;      // =1 if inst fetchs data from memory
}
t_opcode;

/* Opcodes */
t_opcode  base_ops[] = {
        {OP_AABL,     "AABL",       1},
        {OP_AL,       "AL",         1},
        {OP_AU,       "AU",         1},
        {OP_BRNZ,     "BRNZ",       0},
        {OP_BRMIN,    "BRMIN",      0},
        {OP_BRNZU,    "BRNZU",      0},
        {OP_BROV,     "BROV",       0},
        {OP_BRD1,     "BRD1",       0},
        {OP_BRD2,     "BRD2",       0},
        {OP_BRD3,     "BRD3",       0},
        {OP_BRD4,     "BRD4",       0},
        {OP_BRD5,     "BRD5",       0},
        {OP_BRD6,     "BRD6",       0},
        {OP_BRD7,     "BRD7",       0},
        {OP_BRD8,     "BRD8",       0},
        {OP_BRD9,     "BRD9",       0},
        {OP_BRD10,    "BRD10",      0},
        {OP_DIV,      "DIV",        1},
        {OP_DIVRU,    "DIVRU",      1},
        {OP_LD,       "LD",         1},
        {OP_MULT,     "MULT",       1},
        {OP_NOOP,     "NOOP",       0},
        {OP_PCH,      "PCH",        0},
        {OP_RD,       "RD",         0},
        {OP_RAABL,    "RAABL",      1},
        {OP_RAL,      "RAL",        1},
        {OP_RAU,      "RAU",        1},
        {OP_RSABL,    "RSABL",      1},
        {OP_RSL,      "RSL",        1},
        {OP_RSU,      "RSU",        1},
        {OP_SLT,      "SLT",        0},
        {OP_SCT,      "SCT",        0},
        {OP_SRT,      "SRT",        0},
        {OP_SRD,      "SRD",        0},
        {OP_STOP,     "STOP",       0},
        {OP_STD,      "STD",        0},
        {OP_STDA,     "STDA",       0},
        {OP_STIA,     "STIA",       0},
        {OP_STL,      "STL",        0},
        {OP_STU,      "STU",        0},
        {OP_SABL,     "SABL",       1},
        {OP_SL,       "SL",         1},
        {OP_SU,       "SU",         1},
        {OP_TLU,      "TLU",        0},
        {0,           NULL,         0}
};

/* Print out an instruction */
void
print_opcode(FILE * of, t_int64 val, t_opcode * tab)
{

    int sgn;
    int IA; 
    int DA; 
    int op;
    int n;

    if (val < 0) {sgn = -1; val = -val;} else sgn = 1;
    op = Shift_Digits(&val, 2);          // opcode
    DA = Shift_Digits(&val, 4);          // data address
    IA = Shift_Digits(&val, 4);          // intruction address

    while (tab->name != NULL) {
        if (tab->opbase == op) {
            fputs(tab->name, of);
            n = strlen(tab->name);
            while (n++<6) fputc(' ', of);
            fprintf(of, "%04d ", DA);
            fputc(' ', of);
            fprintf(of, "%04d ", IA);
            return;
        }
        tab++;
    }
    fprintf(of, " %d Unknown opcode", op);
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       status code
*/

t_stat
fprint_sym(FILE * of, t_addr addr, t_value * val, UNIT * uptr, int32 sw)
{
    t_int64            inst;
    int                NegZero;
    int ch;

    if (*val == NEGZERO_value) {
        inst = 0;
        NegZero = 1;
    } else {
        inst = *val; 
        NegZero = 0;
    }

    /* Print value in decimal */
    fputc(' ', of);
    fprintf(of, "%06d%04d%c", printfw(inst,NegZero));    // fprintf 10 digits word n, with sign
    inst = AbsWord(inst);

    if (sw & SWMASK('C') ) {
        int                 i;

        fputs("   '", of);
        for (i=0;i<5;i++) {
            ch = Shift_Digits(&inst, 2);
            fputc(mem_to_ascii[ch], of);
        }
        fputc('\'', of);
    }

    if (sw & SWMASK('M')) {
        fputs("   ", of);
        inst = AbsWord(inst);
        print_opcode(of, inst, base_ops);
    }
    return SCPE_OK;
}

t_opcode *
find_opcode(char *op, t_opcode * tab)
{
    while (tab->name != NULL) {
        if (*tab->name != '\0' && strcmp(op, tab->name) == 0)
            return tab;
        tab++;
    }
    return NULL;
}

/* read n digits, optionally with sign NNNN[+|-]

   Inputs:
        *cptr   =       pointer to input string
        sgnFlag =       1 to allow signed value
   Outputs:
        d       =       parsed value
*/

CONST char * parse_sgn(int *neg, CONST char *cptr)
{
    *neg=0;
    while (isspace(*cptr)) cptr++;
    if (*cptr == '+') {
        cptr++; 
    } else if (*cptr == '-') {
        cptr++; *neg = 1;
    }
    return cptr;
}

CONST char * parse_n(t_int64 *d, CONST char *cptr, int n)
{
    int i = 0;

    *d = 0;
    while (1) {
        if ((n == 10) && (isspace(*cptr))) {
            cptr++;  // on 10 digit words, allow spaces
            continue;
        }
        if (*cptr < '0' || *cptr > '9') break;
        if (i++ > n) {
            cptr++;
        } else {
            *d = (*d * 10) + (*cptr++ - '0');
        }
    }
    if (n ==  4) {*d = *d % D4; } else 
    if (n == 10) {*d = *d % D10;}  
    return cptr;
}


/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        uptr    =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

// convert ascii char to two digits IBM 650 code
int ascii_to_NN(int ch)
{
    int i;

    if ((ch >= 'a') && (ch <= 'z')) ch = ch -'a'+'A';
    for (i=0;i<100;i++) if (mem_to_ascii[i] == ch) return i;
    return 0;
}

t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw)
{
    t_int64             d;
    int                 da, ia;
    char                ch, opcode[100];
    t_opcode            *op;
    int                 i;
    int neg, IsNeg;

    while (isspace(*cptr)) cptr++;
    d = 0; IsNeg = 0;
    if (sw & SWMASK('M')) {
        /* Grab opcode */
        cptr = parse_sgn(&neg, cptr);
        if (neg) IsNeg = 1;

        cptr = get_glyph(cptr, opcode, 0);

        op = find_opcode(opcode, base_ops);
        if (op == 0) return STOP_UUO;

        while (isspace(*cptr)) cptr++;
        /* Collect first argument: da */
        cptr = parse_n(&d, cptr, 4);
        da = (int) d; 

        /* Skip blanks */
        while (isspace(*cptr)) cptr++;
        /* Collect second argument: ia */
        cptr = parse_n(&d, cptr, 4);
        ia = (int) d; 
        // construct inst
        d = op->opbase * (t_int64) D8 + da * (t_int64) D4 + (t_int64) ia;
    } else if (sw & SWMASK('C')) {
        d = 0;
        for(i=0; i<5;i++) {
            d = d * 100;
            ch = *cptr; 
            if (ch == '\0') continue;
            cptr++;
            d = d + ascii_to_NN(ch);
        }
    } else {
        cptr = parse_sgn(&neg, cptr);
        if (neg) IsNeg = 1;
        cptr = parse_n(&d, cptr, 10);
    }
    cptr = parse_sgn(&neg, cptr);
    if (neg) IsNeg = 1;
    if ((IsNeg) && (d == 0)) {
        *val = NEGZERO_value; // val has this special value to represent -0 (minus zero == negative zero) 
    } else {
        if (IsNeg) d=-d;
        *val = (t_value) d;
    }
    return SCPE_OK;
}

// get data for opcode
// return pointer to opcode name if opcode found, else NULL
const char * get_opcode_data(int opcode, int * bReadData)
{
    t_opcode * tab = base_ops; 

    *bReadData = 0;
    while (tab->name != NULL) {
        if (tab->opbase == opcode) {
            *bReadData  = tab->bReadData;
            return tab->name;
        }
        tab++;
    }
    return NULL;
}


/* Helper functions */

// set in buf string ascii chars form word d ( chars: c1c2c3c4c5 )
// starts at char start (1..5), for CharLen chars (0..5)
// to convert the full word use (buf, 1, 5, d)
char * word_to_ascii(char * buf, int CharStart, int CharLen, t_int64 d)
{
    int i,c1,c2;
    char * buf0;

    buf0 = buf; // save start of buffer
    for (i=0;i<5;i++) {        // 5 alpha chars per word
        c1 = Shift_Digits(&d, 2);
        c2 = mem_to_ascii[c1];
        if (i < CharStart-1) continue;
        if (i >= CharStart+CharLen-1) continue;
        *buf++ = c2;
    }
    *buf++ = 0;
    return buf0;
}



// return hi digit (digit 10) al leftmost position in number (no sign)
int Get_HiDigit(t_int64 d)  
{
    return (int) ((AbsWord(d) * 10) / D10);
}

// shift d value for nDigits positions (max 7)
// if nDigit > 0 shift left, if < 0 then shift right
// return value of shifted digits (without sign)
int Shift_Digits(t_int64 * d, int nDigits)  
{
    int i,n;
    int neg = 0;

    if (nDigits == 0) return 0;                           // no shift

    if (*d < 0) {*d=-*d; neg = 1;}

    n = 0;
    if (nDigits > 0) {                                    // shift left
        for (i=0;i<nDigits;i++) {
            n  = n * 10 + (int) (*d / (1000000000L));     // nine digits (9 zeroes)
            *d = (*d % (1000000000L)) * 10;      
        }
    } else {                                              // shift right
        for (i=0;i<-nDigits;i++) {
            n = *d % 10;
            *d = *d / 10;      
        }
    }
    if (neg) *d=-*d;
    return n;
}
/* deck operations 

   carddeck [-q] <operation> <parameters...>

                        allowed operations are split, join, print

                        default format for card files is AUTO, this allow to intermix source decks
                        with different formats. To set the format for carddeck operations use

                           set cpr0 -format xxxx
                        
                        this will apply to all operations, both on reading and writing deck files

   carddeck split       split the deck being punched in IBM 533 device in two separate destination decks

                        carddeck split <count> <dev|file0> <file1> <file2>

                        <dev>    should be cdp1 to cdp3. File must be attached. The cards punched on 
                                 this file are the ones on source deck to split.

                        <file0>  if instead of cdp1, cdp2 or cdp3, a file can be specified containing
                                 the source deck to be splitted

                        <count>  number of cards in each splitted deck. 
                                 If count >= 0, indicates the cards on first destination deck file 
                                                remaining cards go to the second destination deck
                                 If count < 0,  indicates the cards on second destination deck file 
                                                (so deck 2 contains lasts count cards from source)

                        <file1>  first destination deck file
                        <file2>  second destination deck file
                                 
                        when using <dev> as source both <file1> or <file2> can have same name as the currently 
                        attached file to cdp device. On command execution, cdp gest its file detached.
                        file1 and file are created (overwritten if already exists).

                        when using <file0> as source both <file1> or <file2> can have same name as <file0>.
                        <file0> is completly read by SimH in its internal buffer (room for 10K cards) 
                        and then splitted to <file1> and <file2>. 

   carddeck join        join several deck files into a new one

                        carddeck join <file1> <file2> ... as <file>

                        <file1>  first source deck file
                        <file2>  second source deck file
                        ...
                        <file>   destination deck file

                        any source file <file1>, <file2>, etc can have same name as destination file <file>.
                        Each source file is completly read in turn by SimH in its internal buffer (room for 10K cards) 
                        and then written on destination file. This allos to append une deck on the top/end of 
                        another one.

   carddeck print       print deck on console, and on simulated IBM 407 is any file is attached to cpd0

                        carddeck print <file>                         

   switches:            if present mut be just after carddeck and before deck operation
    -Q                  quiet return status. 
           
*/

// max number of cards in deck for cadrdeck internal command
#define MAX_CARDS_IN_DECK  10000

// load card file fn and add its cards to 
// DeckImage array, up to a max of nMaxCards
// increment nCards with the number of added cards
// uses cdr0 device/unit
t_stat deck_load(CONST char *fn, uint16 * DeckImage, int * nCards)
{
    UNIT *              uptr = &cdr_unit[0];
    struct _card_data   *data;
    t_stat              r, r2;
    int i, convert_to_ascii;
    uint16 c;

    if (*nCards < 0) {
        *nCards = 0;
        convert_to_ascii = 1;
    } else {
        convert_to_ascii = 0;
    }

    // set flags for read only
    uptr->flags |= UNIT_RO; 

    // attach file to cdr unit 0
    r = (cdr_dev.attach)(uptr, fn);
    if (r != SCPE_OK) return r;

    // read all cards from file
    while (1) {
        if (*nCards >= MAX_CARDS_IN_DECK) {
            r = sim_messagef (SCPE_IERR, "Too many cards\n");
            break;
        }
        r = sim_read_card(uptr);
        if (r == SCPE_EOF) {
            r = SCPE_OK; break;            // normal termination on card file read finished
        } else if (r != SCPE_OK) break;    // abnormal termination on error
        data = (struct _card_data *)uptr->up7;
        // add card read to deck
        for (i=0; i<80; i++) {
            c = data->image[i];
            if (convert_to_ascii) c = data->hol_to_ascii[c];
            DeckImage[*nCards * 80 + i] = c;
        }
        *nCards = *nCards + 1;
    }

    // deattach file from cdr unit 0
    r2 = (cdr_dev.detach)(uptr);
    if (r == SCPE_OK) r = r2; 
    if (r != SCPE_OK) return r;

    return SCPE_OK;
}

// write nCards starting at card from DeckImage array to file fn
// uses cdr0 device/unit
t_stat deck_save(CONST char *fn, uint16 * DeckImage, int card, int nCards)
{
    UNIT *              uptr = &cdr_unit[0];
    struct _card_data   *data;
    t_stat              r;
    int i,nc;

    // set flags for create new file
    uptr->flags &= ~UNIT_RO; 
    sim_switches |= SWMASK ('N');

    // attach file to cdr unit 0
    r = (cdr_dev.attach)(uptr, fn);
    if (r != SCPE_OK) return r;

    // write cards to file
    for  (nc=0;nc<nCards;nc++) {
        if (nc + card >= MAX_CARDS_IN_DECK) {
            r = sim_messagef (SCPE_IERR, "Reading outside of Deck\n");
            break;
        }

        data = (struct _card_data *)uptr->up7;
        // read card from deck
        for (i=0; i<80; i++) data->image[i] = DeckImage[(nc + card) * 80 + i];

        r = sim_punch_card(uptr, NULL);
        if (r != SCPE_OK) break;    // abnormal termination on error
    }

    // deattach file from cdr unit 0
    (cdr_dev.detach)(uptr);

    return r;
}

// carddeck split <count> <dev|file0> <file1> <file2>
static t_stat deck_split_cmd(CONST char *cptr)
{
    char fn0[4*CBUFSIZE];
    char fn1[4*CBUFSIZE];
    char fn2[4*CBUFSIZE];

    char gbuf[4*CBUFSIZE];
    DEVICE *dptr;
    UNIT *uptr;
    t_stat r;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int nCards, nCards1, tail; 

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    if (*cptr == '-') {
        tail = 1;
        cptr++;
    } else {
        tail = 0;
    }
    cptr = get_glyph (cptr, gbuf, 0);                       // get cards count param    
    nCards1 = (int32) get_uint (gbuf, 10, 10000, &r);
    if (r != SCPE_OK) return sim_messagef (SCPE_ARG, "Invalid count value\n");

    cptr = get_glyph (cptr, gbuf, 0);                       // get dev|file0 param    
    if ((strlen(gbuf) != 4) || (strncmp(gbuf, "CDP", 3)) ||
        (gbuf[3] < '1') || (gbuf[3] > '3') ) {
        // is a file
        strcpy(fn0, gbuf);
    } else {
        // is cpd1 cpd2 or cpd3 device
        dptr = find_unit (gbuf, &uptr);                     /* locate unit */
        if (dptr == NULL)                                   /* found dev? */
            return SCPE_NXDEV;
        if (uptr == NULL)                                   /* valid unit? */
            return SCPE_NXUN;
        if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
            return SCPE_NOTATT;
        strcpy(fn0, uptr->filename);
        sim_card_detach(uptr);                              // detach file from cdp device to be splitted
    }

    // read source deck
    nCards = 0;
    r = deck_load(fn0, DeckImage, &nCards);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot read source deck (%s)\n", fn0);

    // calc nCards1 = cards in first deck
    if (tail) {
        // calc cards remaining when last nCardCount are removed from source deck
        nCards1 = nCards - nCards1;
        if (nCards1 < 0) nCards1 = 0;
    }
    if (nCards1 > nCards) nCards1 = nCards;

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fn1, 0);                 // get next param: filename 1
    if (fn1[0] == 0) return sim_messagef (SCPE_ARG, "Missing first filename\n");
    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fn2, 0);                 // get next param: filename 2
    if (fn2[0] == 0) return sim_messagef (SCPE_ARG, "Missing second filename\n");
    
    r = deck_save(fn1, DeckImage, 0, nCards1);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck1 (%s)\n", fn0);

    r = deck_save(fn2, DeckImage, nCards1, nCards-nCards1);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck2 (%s)\n", fn0);

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Deck splitted to %d/%d cards\n", nCards1, nCards-nCards1);
    }
    return SCPE_OK;

}

// carddeck join <file1> <file2> ... as <file>
static t_stat deck_join_cmd(CONST char *cptr)
{
    char fnSrc[4*CBUFSIZE];
    char fnDest[4*CBUFSIZE];
    CONST char *cptr0;
    CONST char *cptrAS;
    char gbuf[4*CBUFSIZE];
    t_stat r;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int i,nDeck, nCards, nCards1;

    cptr0 = cptr;
    // look for "as"
    while (*cptr) {
        while (sim_isspace (*cptr)) cptr++;                 // trim leading spc 
        cptrAS = cptr; // mark position of AS
        cptr = get_glyph_quoted (cptr, gbuf, 0);            // get next param
        if (gbuf[0] == 0) return sim_messagef (SCPE_ARG, "AS <file> not found\n");
        for (i=0;i<2;i++) gbuf[i] = sim_toupper(gbuf[i]);
        if (strcmp(gbuf, "AS") == 0) break;
    }

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fnDest, 0);              // get next param: destination filename 
    if (fnDest[0] == 0) return sim_messagef (SCPE_ARG, "Missing destination filename\n");
    if (*cptr) return sim_messagef (SCPE_ARG, "Extra unknown parameters after destination filename\n");

    cptr = cptr0;                                           // restore cptr to scan source filenames
    nDeck = nCards = 0;
    memset(DeckImage, 0, sizeof(DeckImage));
    while (1) {

        while (sim_isspace (*cptr)) cptr++;                 // trim leading spc 
        if (cptrAS == cptr) break;                          // break if reach "AS"
        cptr = get_glyph_quoted (cptr, fnSrc, 0);           // get next param: source filename 
        if (fnSrc[0] == 0) return sim_messagef (SCPE_ARG, "Missing source filename\n");

        // read source deck
        nCards1 = nCards;
        r = deck_load(fnSrc, DeckImage, &nCards);
        if (r != SCPE_OK) return sim_messagef (r, "Cannot read source deck (%s)\n", fnSrc);
        nDeck++;

        if ((sim_switches & SWMASK ('Q')) == 0) {
            sim_messagef (SCPE_OK, "Source Deck %d has %d cards (%s)\n", nDeck, nCards - nCards1, fnSrc);
        }
    }
    r = deck_save(fnDest, DeckImage, 0, nCards);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck (%s)\n", fnDest);

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Destination Deck has %d cards (%s)\n", nCards, fnDest);
    }
    
    return SCPE_OK;
}

// carddeck print <file> 
static t_stat deck_print_cmd(CONST char *cptr)
{
    char fn[4*CBUFSIZE];
    char line[81]; 
    t_stat r;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int i,c,nc,nCards;

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fn, 0);                  // get next param: source filename 
    if (fn[0] == 0) return sim_messagef (SCPE_ARG, "Missing filename\n");
    if (*cptr) return sim_messagef (SCPE_ARG, "Extra unknown parameters after filename\n");

    // read deck to be printed (-1 to convert to ascii value, not hol)
    nCards = -1;
    r = deck_load(fn, DeckImage, &nCards);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot read deck to print (%s)\n", fn);

    for (nc=0; nc<nCards; nc++) {
        // read card, check and, store in line
        for (i=0;i<80;i++) {
            c = DeckImage[nc * 80 + i];
            c = toupper(c);                             // IBM 407 can only print uppercase
            if ((c == '?') || (c == '!')) c = '0';      // remove Y(12) or X(11) punch on zero 
            if (strchr(mem_to_ascii, c) == 0) c = ' ';  // space if not in IBM 650 character set
            line[i] = c;
        }
        line[80]=0;
        sim_trim_endspc(line); 
        // echo on console (add CR LF)
        for (i=0;i<(int)strlen(line);i++) sim_putchar(line[i]);     
        sim_putchar(13);sim_putchar(10);
        // printout will be directed to file attached to CDP0 unit, if any
        if (cdp_unit[0].flags & UNIT_ATT) {
            sim_fwrite(line, 1, strlen(line), cdp_unit[0].fileref); // fwrite clears line!
            line[0] = 13; line[1] = 10; line[2] = 0;  
            sim_fwrite(line, 1, 2, cdp_unit[0].fileref); 
        }
    }

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Printed Deck with %d cards (%s)\n", nCards, fn);
    }
    
    return SCPE_OK;
}

static t_stat ibm650_deck_cmd(int32 arg, CONST char *buf)
{
    char gbuf[4*CBUFSIZE];
    const char *cptr;

    cptr = get_glyph (buf, gbuf, 0);                   // get next param
    if (strcmp(gbuf, "-Q") == 0) {
        sim_switches |= SWMASK ('Q');
        cptr = get_glyph (cptr, gbuf, 0);
    }

    if (strcmp(gbuf, "JOIN") == 0) {
        return deck_join_cmd(cptr);
    }
    if (strcmp(gbuf, "SPLIT") == 0) {
        return deck_split_cmd(cptr);
    }
    if (strcmp(gbuf, "PRINT") == 0) {
        return deck_print_cmd(cptr);
    }
    return sim_messagef (SCPE_ARG, "Unknown deck command operation\n");
}




