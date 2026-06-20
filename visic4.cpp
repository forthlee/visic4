// VisiC4.cpp - Visual C4 Compiler/Interpreter IDE
// Reimplementation using modern Turbo Vision windowing system
// Based on VisiCLANG (C) 1990 Dept. ECE, University of Limerick

#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TSubMenu
#define Uses_TWindow
#define Uses_TView
#define Uses_TRect
#define Uses_TEvent
#define Uses_TScrollBar
#define Uses_TScroller
#define Uses_TStaticText
#define Uses_TDialog
#define Uses_TButton
#define Uses_MsgBox
#define Uses_TListViewer
#define Uses_TFrame
#define Uses_TFileDialog
#define Uses_TProgram
#define Uses_TLabel
#include <tvision/tv.h>

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <set>
#include <chrono>

// Custom commands
enum : ushort
{
    cmCompile = 1000,
    cmRun,
    cmCycle,
    cmStepOver,
    cmRestart,
    cmLoadSource,
    cmTileWindows,
    cmCascadeWindows,
    cmSwitchMode,
    cmShowClang,
    cmShowGrammar,
    cmShowProcList,
    cmShowInput,
    cmShowCode,
    cmShowStack,
    cmShowDisplay,
    cmShowIO,
    cmShowSource,
    cmCompileSpeed,
    cmClearBreaks,
    cmContinueTrace,
    cmStop,
};

enum AppMode { mCompile, mRuntime };

/////////////////////////////////////////////////////////////////////////
// C4 Tokenizer for compile trace

enum C4Token {
    C4_EOF = 0,
    C4_Num = 128, C4_Fun, C4_Sys, C4_Glo, C4_Loc, C4_Id,
    C4_Char, C4_Else, C4_Enum, C4_If, C4_Int, C4_Return, C4_Sizeof, C4_While,
    C4_Assign, C4_Cond, C4_Lor, C4_Lan, C4_Or, C4_Xor, C4_And,
    C4_Eq, C4_Ne, C4_Lt, C4_Gt, C4_Le, C4_Ge, C4_Shl, C4_Shr,
    C4_Add, C4_Sub, C4_Mul, C4_Div, C4_Mod, C4_Inc, C4_Dec, C4_Brak
};

// Opcodes for code generation
static const char *c4_opnames[] = {
    "LEA", "IMM", "JMP", "JSR", "BZ",  "BNZ", "ENT", "ADJ", "LEV",
    "LI",  "LC",  "SI",  "SC",  "PSH",
    "OR",  "XOR", "AND", "EQ",  "NE",  "LT",  "GT",  "LE",  "GE",
    "SHL", "SHR", "ADD", "SUB", "MUL", "DIV", "MOD",
    "OPEN","READ","CLOS","PRTF","MALC","FREE","MSET","MCMP","EXIT",
    nullptr
};
enum C4Op {
    OP_LEA, OP_IMM, OP_JMP, OP_JSR, OP_BZ, OP_BNZ, OP_ENT, OP_ADJ, OP_LEV,
    OP_LI, OP_LC, OP_SI, OP_SC, OP_PSH,
    OP_OR, OP_XOR, OP_AND, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_SHL, OP_SHR, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_OPEN, OP_READ, OP_CLOS, OP_PRTF, OP_MALC, OP_FREE, OP_MSET, OP_MCMP, OP_EXIT,
    OP_COUNT
};

// Map opcode to c40.c VM instruction line (0-based)
static const int vmLineForOp[] = {
    486, 487, 488, 489, 490, 491, 492, 493, 494, // LEA..LEV
    495, 496, 497, 498, 499,                       // LI..PSH
    501, 502, 503, 504, 505, 506, 507, 508, 509,  // OR..GE
    510, 511, 512, 513, 514, 515, 516,             // SHL..MOD
    518, 519, 520, 521, 522, 523, 524, 525, 526,   // OPEN..EXIT
};

// C4 source line mapping (0-based index into embedded c4 source)
// These correspond to the lines in the next() function that handle each token type
enum C4SourceLine {
    C4L_NEXT_NEWLINE    = 54, // if (tk == '\n')
    C4L_NEXT_HASH       = 65, // else if (tk == '#')
    C4L_NEXT_IDENT      = 68, // else if ((tk >= 'a' ...
    C4L_NEXT_NUMBER     = 83, // else if (tk >= '0' ...
    C4L_NEXT_SLASH      = 93, // else if (tk == '/')
    C4L_NEXT_STRCHAR    = 103, // else if (tk == '\'' || tk == '"')
    C4L_NEXT_EQ         = 115, // else if (tk == '=')
    C4L_NEXT_PLUS       = 116, // else if (tk == '+')
    C4L_NEXT_MINUS      = 117, // else if (tk == '-')
    C4L_NEXT_BANG       = 118, // else if (tk == '!')
    C4L_NEXT_LT         = 119, // else if (tk == '<')
    C4L_NEXT_GT         = 120, // else if (tk == '>')
    C4L_NEXT_PIPE       = 121, // else if (tk == '|')
    C4L_NEXT_AMP        = 122, // else if (tk == '&')
    C4L_NEXT_CARET      = 123, // else if (tk == '^')
    C4L_NEXT_PERCENT    = 124, // else if (tk == '%')
    C4L_NEXT_STAR       = 125, // else if (tk == '*')
    C4L_NEXT_LBRAK      = 126, // else if (tk == '[')
    C4L_NEXT_QUEST      = 127, // else if (tk == '?')
    C4L_NEXT_MISC       = 128, // else if (tk == '~' || ...
    C4L_EXPR            = 132, // void expr(int lev)
    C4L_STMT            = 282, // void stmt()
    C4L_MAIN_PARSE      = 375, // while (tk) { -- parse declarations
};

// Grammar line mapping (0-based index into grammar window)
enum GrammarLine {
    GR_PROGRAM      = 0,  // <program>
    GR_GLOBAL_DECL  = 1,  // <global_decl>
    GR_GLOBAL_DECL2 = 2,  // continuation
    GR_TYPE         = 3,  // <type>
    GR_FUNC_DEF     = 4,  // <func_def>
    GR_PARAMS       = 5,  // <params>
    GR_BODY         = 6,  // <body>
    GR_LOCAL_DECL   = 7,  // <local_decl>
    GR_STMT         = 8,  // <stmt>
    GR_STMT2        = 9,  // while
    GR_STMT3        = 10, // return
    GR_STMT4        = 11, // block
    GR_STMT5        = 12, // expr stmt
    GR_EXPR         = 13, // <expr>
    GR_ASSIGN       = 14, // <assign>
    GR_COND         = 15, // <cond>
    GR_LOR          = 16, // <lor>
    GR_LAN          = 17, // <lan>
    GR_OR           = 18, // <or>
    GR_XOR          = 19, // <xor>
    GR_AND          = 20, // <and>
    GR_EQ           = 21, // <eq>
    GR_REL          = 22, // <rel>
    GR_SHIFT        = 23, // <shift>
    GR_ADD          = 24, // <add>
    GR_MUL          = 25, // <mul>
    GR_UNARY        = 26, // <unary>
    GR_POSTFIX      = 27, // <postfix>
    GR_PRIMARY      = 28, // <primary>
};

struct CompileTrace {
    // Source being compiled
    std::string src;
    const char *p;       // current position
    int line;            // current source line (1-based)
    int tk;              // current token
    int ival;            // integer value
    std::string idName;  // identifier name

    // Code generation
    int codeAddr;

    // State machine
    bool active {false};
    bool done {false};

    // Step-through: queue of C4 source lines to highlight before emitting token
    std::vector<int> c4Lines;  // pending C4 lines to show
    size_t c4Pos {0};          // current position in c4Lines
    int grammarLine {-1};      // grammar line for current token
    bool tokenEmitted {true};  // true = need to call next() for new token

    // Keyword table
    struct Keyword { const char *name; int token; };
    static constexpr Keyword keywords[] = {
        {"char", C4_Char}, {"else", C4_Else}, {"enum", C4_Enum},
        {"if", C4_If}, {"int", C4_Int}, {"return", C4_Return},
        {"sizeof", C4_Sizeof}, {"while", C4_While},
        {nullptr, 0}
    };

    void init(const std::vector<std::string> &lines)
    {
        src.clear();
        for (auto &l : lines)
            src += l + "\n";
        p = src.c_str();
        line = 1;
        tk = 0;
        ival = 0;
        codeAddr = 0;
        active = true;
        done = false;
        c4Lines.clear();
        c4Pos = 0;
        grammarLine = -1;
        tokenEmitted = true;
    }

    // Get token name for display
    std::string tokenName() const
    {
        if (tk == C4_Num) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Num(%d)", ival);
            return buf;
        }
        if (tk == C4_Id) return "Id(" + idName + ")";
        if (tk == C4_Char) return "Char";
        if (tk == C4_Else) return "Else";
        if (tk == C4_Enum) return "Enum";
        if (tk == C4_If) return "If";
        if (tk == C4_Int) return "Int";
        if (tk == C4_Return) return "Return";
        if (tk == C4_Sizeof) return "Sizeof";
        if (tk == C4_While) return "While";
        if (tk == C4_Assign) return "Assign(=)";
        if (tk == C4_Cond) return "Cond(?)";
        if (tk == C4_Lor) return "Lor(||)";
        if (tk == C4_Lan) return "Lan(&&)";
        if (tk == C4_Or) return "Or(|)";
        if (tk == C4_Xor) return "Xor(^)";
        if (tk == C4_And) return "And(&)";
        if (tk == C4_Eq) return "Eq(==)";
        if (tk == C4_Ne) return "Ne(!=)";
        if (tk == C4_Lt) return "Lt(<)";
        if (tk == C4_Gt) return "Gt(>)";
        if (tk == C4_Le) return "Le(<=)";
        if (tk == C4_Ge) return "Ge(>=)";
        if (tk == C4_Shl) return "Shl(<<)";
        if (tk == C4_Shr) return "Shr(>>)";
        if (tk == C4_Add) return "Add(+)";
        if (tk == C4_Sub) return "Sub(-)";
        if (tk == C4_Mul) return "Mul(*)";
        if (tk == C4_Div) return "Div(/)";
        if (tk == C4_Mod) return "Mod(%)";
        if (tk == C4_Inc) return "Inc(++)";
        if (tk == C4_Dec) return "Dec(--)";
        if (tk == C4_Brak) return "Brak([)";
        if (tk > 0 && tk < 128) {
            char buf[8];
            snprintf(buf, sizeof(buf), "'%c'", tk);
            return buf;
        }
        return "EOF";
    }

    // Build the C4 line trace for the current token type.
    // These are the lines in next() that would execute (0-based).
    // Common prefix: enter while loop, read char, ++p
    void buildC4Trace()
    {
        c4Lines.clear();
        c4Pos = 0;

        // next() entry: while ((tk = *p)) { ++p;
        c4Lines.push_back(52); // while ((tk = *p))
        c4Lines.push_back(53); // ++p

        if (tk == C4_Id || (tk >= C4_Char && tk <= C4_While)) {
            // identifier/keyword path: lines 68-82
            c4Lines.push_back(68); // else if ((tk >= 'a' ...
            c4Lines.push_back(69); // pp = p - 1
            c4Lines.push_back(70); // while ((*p >= 'a' ...
            c4Lines.push_back(71); //   tk = tk * 147 + *p++
            c4Lines.push_back(72); // tk = (tk << 6) + (p - pp)
            c4Lines.push_back(73); // id = sym
            c4Lines.push_back(74); // while (id[Tk])
            if (tk >= C4_Char && tk <= C4_While) {
                // keyword found in symbol table
                c4Lines.push_back(75); // if (tk == id[Hash] ...) { tk = id[Tk]; return; }
            } else {
                // new identifier
                c4Lines.push_back(78); // id[Name] = (int)pp
                c4Lines.push_back(79); // id[Hash] = tk
                c4Lines.push_back(80); // tk = id[Tk] = Id
                c4Lines.push_back(81); // return
            }
            grammarLine = GR_PRIMARY;
        }
        else if (tk == C4_Num) {
            // number path: lines 83-91
            c4Lines.push_back(83); // else if (tk >= '0' ...
            c4Lines.push_back(84); // if ((ival = tk - '0'))
            c4Lines.push_back(85); //   while ... ival = ival * 10 ...
            c4Lines.push_back(90); // tk = Num
            c4Lines.push_back(91); // return
            grammarLine = GR_PRIMARY;
        }
        else if (tk == C4_Div) {
            c4Lines.push_back(93);  // else if (tk == '/')
            c4Lines.push_back(98);  // tk = Div
            c4Lines.push_back(100); // return
            grammarLine = GR_MUL;
        }
        else if (tk == C4_Assign || tk == C4_Eq) {
            c4Lines.push_back(115); // else if (tk == '=')
            grammarLine = (tk == C4_Eq) ? GR_EQ : GR_ASSIGN;
        }
        else if (tk == C4_Add || tk == C4_Inc) {
            c4Lines.push_back(116); // else if (tk == '+')
            grammarLine = (tk == C4_Inc) ? GR_POSTFIX : GR_ADD;
        }
        else if (tk == C4_Sub || tk == C4_Dec) {
            c4Lines.push_back(117); // else if (tk == '-')
            grammarLine = (tk == C4_Dec) ? GR_POSTFIX : GR_ADD;
        }
        else if (tk == C4_Ne || tk == '!') {
            c4Lines.push_back(118); // else if (tk == '!')
            grammarLine = GR_UNARY;
        }
        else if (tk == C4_Lt || tk == C4_Le || tk == C4_Shl) {
            c4Lines.push_back(119); // else if (tk == '<')
            grammarLine = (tk == C4_Le || tk == C4_Lt) ? GR_REL : GR_SHIFT;
        }
        else if (tk == C4_Gt || tk == C4_Ge || tk == C4_Shr) {
            c4Lines.push_back(120); // else if (tk == '>')
            grammarLine = (tk == C4_Ge || tk == C4_Gt) ? GR_REL : GR_SHIFT;
        }
        else if (tk == C4_Lor || tk == C4_Or) {
            c4Lines.push_back(121); // else if (tk == '|')
            grammarLine = (tk == C4_Lor) ? GR_LOR : GR_OR;
        }
        else if (tk == C4_Lan || tk == C4_And) {
            c4Lines.push_back(122); // else if (tk == '&')
            grammarLine = (tk == C4_Lan) ? GR_LAN : GR_AND;
        }
        else if (tk == C4_Xor) {
            c4Lines.push_back(123);
            grammarLine = GR_XOR;
        }
        else if (tk == C4_Mod) {
            c4Lines.push_back(124);
            grammarLine = GR_MUL;
        }
        else if (tk == C4_Mul) {
            c4Lines.push_back(125);
            grammarLine = GR_MUL;
        }
        else if (tk == C4_Brak) {
            c4Lines.push_back(126);
            grammarLine = GR_POSTFIX;
        }
        else if (tk == C4_Cond) {
            c4Lines.push_back(127);
            grammarLine = GR_COND;
        }
        else {
            // single-char tokens: ~ ; { } ( ) ] , :
            c4Lines.push_back(128); // else if (tk == '~' || ...
            if (tk == C4_If) grammarLine = GR_STMT;
            else if (tk == C4_While) grammarLine = GR_STMT2;
            else if (tk == C4_Return) grammarLine = GR_STMT3;
            else if (tk == '{') grammarLine = GR_STMT4;
            else if (tk == ';') grammarLine = GR_STMT5;
            else if (tk == '(') grammarLine = GR_PRIMARY;
            else grammarLine = GR_PROGRAM;
        }

        // --- expr() lines: primary/unary prefix handling (c40.c lines 137-215) ---
        if (tk == C4_Num)    { c4Lines.push_back(137); } // else if (tk == Num)
        if (tk == C4_Id)     { c4Lines.push_back(151); c4Lines.push_back(152); } // else if (tk == Id) { d = id; next();
        if (tk == C4_Sizeof) { c4Lines.push_back(143); c4Lines.push_back(144); c4Lines.push_back(148); c4Lines.push_back(149); }
        if (tk == C4_Add)    { c4Lines.push_back(198); } // unary +
        if (tk == C4_Sub)    { c4Lines.push_back(199); c4Lines.push_back(200); c4Lines.push_back(202); } // unary -
        if (tk == C4_Mul)    { c4Lines.push_back(186); c4Lines.push_back(187); c4Lines.push_back(189); } // dereference
        if (tk == C4_And)    { c4Lines.push_back(191); c4Lines.push_back(192); c4Lines.push_back(194); } // address-of
        if (tk == C4_Inc || tk == C4_Dec) { c4Lines.push_back(204); c4Lines.push_back(205); c4Lines.push_back(212); } // pre-inc/dec

        // --- expr() lines: binary operator handling (c40.c lines 217-280) ---
        // Precedence climbing loop entry
        if (tk == C4_Assign || tk == C4_Eq || tk == C4_Ne ||
            tk == C4_Lt || tk == C4_Gt || tk == C4_Le || tk == C4_Ge ||
            tk == C4_Shl || tk == C4_Shr ||
            tk == C4_Add || tk == C4_Sub || tk == C4_Mul || tk == C4_Div || tk == C4_Mod ||
            tk == C4_Or || tk == C4_Xor || tk == C4_And ||
            tk == C4_Lor || tk == C4_Lan ||
            tk == C4_Cond || tk == C4_Inc || tk == C4_Dec || tk == C4_Brak) {
            c4Lines.push_back(216); // while (tk >= lev)
            c4Lines.push_back(217); // t = ty;
        }
        if (tk == C4_Assign) { c4Lines.push_back(218); c4Lines.push_back(219); c4Lines.push_back(221); }
        if (tk == C4_Cond)   { c4Lines.push_back(223); c4Lines.push_back(224); c4Lines.push_back(226); }
        if (tk == C4_Lor)    { c4Lines.push_back(232); }
        if (tk == C4_Lan)    { c4Lines.push_back(233); }
        if (tk == C4_Or)     { c4Lines.push_back(234); }
        if (tk == C4_Xor)    { c4Lines.push_back(235); }
        if (tk == C4_And)    { c4Lines.push_back(236); }
        if (tk == C4_Eq)     { c4Lines.push_back(237); }
        if (tk == C4_Ne)     { c4Lines.push_back(238); }
        if (tk == C4_Lt)     { c4Lines.push_back(239); }
        if (tk == C4_Gt)     { c4Lines.push_back(240); }
        if (tk == C4_Le)     { c4Lines.push_back(241); }
        if (tk == C4_Ge)     { c4Lines.push_back(242); }
        if (tk == C4_Shl)    { c4Lines.push_back(243); }
        if (tk == C4_Shr)    { c4Lines.push_back(244); }
        if (tk == C4_Add)    { c4Lines.push_back(245); c4Lines.push_back(246); c4Lines.push_back(248); } // binary +
        if (tk == C4_Sub)    { c4Lines.push_back(250); c4Lines.push_back(251); c4Lines.push_back(254); } // binary -
        if (tk == C4_Mul)    { c4Lines.push_back(256); } // binary *
        if (tk == C4_Div)    { c4Lines.push_back(257); }
        if (tk == C4_Mod)    { c4Lines.push_back(258); }
        if (tk == C4_Inc || tk == C4_Dec) { c4Lines.push_back(259); c4Lines.push_back(264); c4Lines.push_back(268); } // post-inc/dec
        if (tk == C4_Brak)   { c4Lines.push_back(270); c4Lines.push_back(271); c4Lines.push_back(275); c4Lines.push_back(276); }

        // --- stmt() lines (c40.c lines 283-329) ---
        if (tk == C4_If)     { c4Lines.push_back(286); c4Lines.push_back(287); c4Lines.push_back(289); c4Lines.push_back(291); c4Lines.push_back(292); }
        if (tk == C4_While)  { c4Lines.push_back(300); c4Lines.push_back(301); c4Lines.push_back(302); c4Lines.push_back(304); c4Lines.push_back(306); c4Lines.push_back(307); }
        if (tk == C4_Return) { c4Lines.push_back(311); c4Lines.push_back(312); c4Lines.push_back(314); c4Lines.push_back(315); }
        if (tk == '{')       { c4Lines.push_back(317); c4Lines.push_back(318); c4Lines.push_back(320); }
        if (tk == ';')       { c4Lines.push_back(322); c4Lines.push_back(323); }

        // --- main() parse-declarations lines (c40.c lines 373-466) ---
        c4Lines.push_back(375); // while (tk)
        if (tk == C4_Int)    { c4Lines.push_back(377); } // if (tk == Int) next();
        if (tk == C4_Char)   { c4Lines.push_back(378); } // else if (tk == Char)
        if (tk == C4_Enum)   { c4Lines.push_back(379); c4Lines.push_back(380); c4Lines.push_back(382); }
        if (tk == C4_Id)     { c4Lines.push_back(400); c4Lines.push_back(403); c4Lines.push_back(405); } // while (tk != ';'), id check
        if (tk == C4_Mul)    { c4Lines.push_back(402); } // pointer type: while (tk == Mul)
        if (tk == '(')       { c4Lines.push_back(407); c4Lines.push_back(408); c4Lines.push_back(410); } // function def
        if (tk == ')')       { c4Lines.push_back(424); } // end params
        if (tk == '{')       { c4Lines.push_back(425); c4Lines.push_back(426); c4Lines.push_back(427); } // function body
        if (tk == '}')       { c4Lines.push_back(445); c4Lines.push_back(446); c4Lines.push_back(447); c4Lines.push_back(448); } // end function, unwind
        if (tk == ',')       { c4Lines.push_back(422); } // param separator
        if (tk == ';')       { c4Lines.push_back(464); } // declaration terminator
        if (tk == C4_Assign) { c4Lines.push_back(388); c4Lines.push_back(389); } // enum assign
    }

    // Returns current C4 highlight line, or -1 if trace for this token is done
    int currentC4Line() const
    {
        if (c4Pos < c4Lines.size())
            return c4Lines[c4Pos];
        return -1;
    }

    // Advance to next C4 line. Returns true if more lines remain.
    bool advanceC4Line()
    {
        if (c4Pos < c4Lines.size())
            ++c4Pos;
        return c4Pos < c4Lines.size();
    }

    // Tokenize next token. Returns false when source is exhausted.
    bool next()
    {
        while (*p) {
            tk = *p++;

            if (tk == '\n') {
                ++line;
                continue;
            }
            if (tk == ' ' || tk == '\t' || tk == '\r') continue;

            // preprocessor - skip line
            if (tk == '#') {
                while (*p && *p != '\n') ++p;
                continue;
            }

            // identifier or keyword
            if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') || tk == '_') {
                const char *start = p - 1;
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '_')
                    ++p;
                idName.assign(start, p - start);
                for (int i = 0; keywords[i].name; ++i) {
                    if (idName == keywords[i].name) {
                        tk = keywords[i].token;
                        buildC4Trace();
                        tokenEmitted = false;
                        return true;
                    }
                }
                tk = C4_Id;
                buildC4Trace();
                tokenEmitted = false;
                return true;
            }

            // number
            if (tk >= '0' && tk <= '9') {
                ival = tk - '0';
                if (ival) {
                    while (*p >= '0' && *p <= '9')
                        ival = ival * 10 + *p++ - '0';
                } else if (*p == 'x' || *p == 'X') {
                    ++p;
                    while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
                        ival = ival * 16 + (*p & 15) + (*p >= 'A' ? 9 : 0), ++p;
                } else {
                    while (*p >= '0' && *p <= '7')
                        ival = ival * 8 + *p++ - '0';
                }
                tk = C4_Num;
                buildC4Trace();
                tokenEmitted = false;
                return true;
            }

            // comment or divide
            if (tk == '/') {
                if (*p == '/') {
                    ++p;
                    while (*p && *p != '\n') ++p;
                    continue;
                }
                tk = C4_Div;
                buildC4Trace();
                tokenEmitted = false;
                return true;
            }

            // string / char literal
            if (tk == '\'' || tk == '"') {
                int quote = tk;
                while (*p && *p != quote) {
                    if (*p == '\\') ++p;
                    if (*p) ++p;
                }
                if (*p) ++p;
                tk = C4_Num;
                buildC4Trace();
                tokenEmitted = false;
                return true;
            }

            // two-char operators
            if (tk == '=') { if (*p == '=') { ++p; tk = C4_Eq; } else tk = C4_Assign; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '+') { if (*p == '+') { ++p; tk = C4_Inc; } else tk = C4_Add; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '-') { if (*p == '-') { ++p; tk = C4_Dec; } else tk = C4_Sub; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '!') { if (*p == '=') { ++p; tk = C4_Ne; } buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '<') { if (*p == '=') { ++p; tk = C4_Le; } else if (*p == '<') { ++p; tk = C4_Shl; } else tk = C4_Lt; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '>') { if (*p == '=') { ++p; tk = C4_Ge; } else if (*p == '>') { ++p; tk = C4_Shr; } else tk = C4_Gt; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '|') { if (*p == '|') { ++p; tk = C4_Lor; } else tk = C4_Or; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '&') { if (*p == '&') { ++p; tk = C4_Lan; } else tk = C4_And; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '^') { tk = C4_Xor; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '%') { tk = C4_Mod; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '*') { tk = C4_Mul; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '[') { tk = C4_Brak; buildC4Trace(); tokenEmitted = false; return true; }
            if (tk == '?') { tk = C4_Cond; buildC4Trace(); tokenEmitted = false; return true; }

            // single-char tokens
            if (tk == '~' || tk == ';' || tk == '{' || tk == '}' ||
                tk == '(' || tk == ')' || tk == ']' || tk == ',' || tk == ':') {
                buildC4Trace();
                tokenEmitted = false;
                return true;
            }
        }
        tk = C4_EOF;
        done = true;
        active = false;
        return false;
    }

    // Generate a code line for display
    std::string emitCode(int op, int arg = 0, bool hasArg = false)
    {
        char buf[64];
        if (hasArg)
            snprintf(buf, sizeof(buf), "%4d %-4s %d", codeAddr++, c4_opnames[op], arg);
        else
            snprintf(buf, sizeof(buf), "%4d %-4s", codeAddr++, c4_opnames[op]);
        return buf;
    }
};

constexpr CompileTrace::Keyword CompileTrace::keywords[];

/////////////////////////////////////////////////////////////////////////
// C4 Mini-Compiler — compiles source to bytecode for the Code window
// Follows c40.c logic closely for correct code generation.

class C4Compiler {
    enum { // tokens
        Num = 128, Fun, Sys, Glo, Loc, Id,
        Char, Else, Enum, If, Int, Return, Sizeof, While,
        Assign, Cond, Lor, Lan, Or, Xor, And,
        Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr,
        Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
    };
    enum { // opcodes
        _LEA,_IMM,_JMP,_JSR,_BZ,_BNZ,_ENT,_ADJ,_LEV,
        _LI,_LC,_SI,_SC,_PSH,
        _OR,_XOR,_AND,_EQ,_NE,_LT,_GT,_LE,_GE,
        _SHL,_SHR,_ADD,_SUB,_MUL,_DIV,_MOD,
        _OPEN,_READ,_CLOS,_PRTF,_MALC,_FREE,_MSET,_MCMP,_EXIT
    };
    enum { CHAR_, INT_, PTR_ };
    enum { Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz };

    // Use intptr_t to store pointers safely on 64-bit (C4 uses #define int long long)
    typedef intptr_t word;

public:
    // Per-function variable info (saved before locals are unwound)
    struct VarInfo {
        std::string name;
        int leaOffset; // bp + leaOffset to access
        bool isParam;  // true=parameter, false=local
    };
    struct FuncInfo {
        std::string name;
        word *entry;  // code entry address
        word *end;    // address after last LEV
        int loc;      // loc value for this function
        std::vector<VarInfo> vars;
    };
    std::vector<FuncInfo> functions;

private:
    std::vector<word> symBuf, codeBuf;
    std::vector<char> dataBuf;
    word *sym, *id, *e;
    char *data;
    const char *p;
    word tk, ival, ty, loc, line;

    void next() {
        while (*p) {
            tk = *p++;
            if (tk == '\n') { ++line; continue; }
            if (tk <= ' ') continue;
            if (tk == '#') { while (*p && *p != '\n') ++p; continue; }
            if (tk == '/' && *p == '/') { while (*p && *p != '\n') ++p; continue; }
            if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') || tk == '_') {
                const char *pp = p - 1;
                word h = tk;
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '_')
                    h = h * 147 + *p++;
                h = (h << 6) + (word)(p - pp);
                id = sym;
                while (id[Tk]) {
                    if (h == id[Hash] && !memcmp((const char *)id[Name], pp, p - pp)) { tk = id[Tk]; return; }
                    id = id + Idsz;
                }
                id[Name] = (word)pp;
                id[Hash] = h;
                tk = id[Tk] = Id;
                return;
            }
            if (tk >= '0' && tk <= '9') {
                ival = tk - '0';
                if (ival) { while (*p >= '0' && *p <= '9') ival = ival * 10 + *p++ - '0'; }
                else if (*p == 'x' || *p == 'X') {
                    ++p;
                    while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
                        ival = ival * 16 + (*p & 15) + (*p >= 'A' ? 9 : 0), ++p;
                }
                tk = Num; return;
            }
            if (tk == '\'' || tk == '"') {
                word q = tk;
                ival = (word)data;
                while (*p && *p != q) {
                    word ch = *p++;
                    if (ch == '\\') {
                        ch = *p++;
                        if (ch == 'n') ch = '\n';
                        else if (ch == 't') ch = '\t';
                        else if (ch == '0') ch = '\0';
                    }
                    if (q == '"') *data++ = (char)ch;
                    else ival = ch;
                }
                ++p;
                if (q == '"') { *data++ = 0; while ((word)data & (sizeof(word)-1)) *data++ = 0; }
                tk = Num;
                return;
            }
            if (tk == '=') { if (*p == '=') { ++p; tk = Eq; } else tk = Assign; return; }
            if (tk == '+') { if (*p == '+') { ++p; tk = Inc; } else tk = Add; return; }
            if (tk == '-') { if (*p == '-') { ++p; tk = Dec; } else tk = Sub; return; }
            if (tk == '!') { if (*p == '=') { ++p; tk = Ne; } else tk = '!'; return; }
            if (tk == '<') { if (*p == '=') { ++p; tk = Le; } else if (*p == '<') { ++p; tk = Shl; } else tk = Lt; return; }
            if (tk == '>') { if (*p == '=') { ++p; tk = Ge; } else if (*p == '>') { ++p; tk = Shr; } else tk = Gt; return; }
            if (tk == '|') { if (*p == '|') { ++p; tk = Lor; } else tk = Or; return; }
            if (tk == '&') { if (*p == '&') { ++p; tk = Lan; } else tk = And; return; }
            if (tk == '^') { tk = Xor; return; }
            if (tk == '%') { tk = Mod; return; }
            if (tk == '*') { tk = Mul; return; }
            if (tk == '[') { tk = Brak; return; }
            if (tk == '?') { tk = Cond; return; }
            if (tk == '/') { tk = Div; return; }
            return; // single char tokens: ; { } ( ) ] , : ~
        }
        tk = 0;
    }

    void expr(word lev) {
        word t, *d;
        if (!tk) return;
        if (tk == Num) { *++e = _IMM; *++e = ival; next(); ty = INT_; }
        else if (tk == Id) {
            d = id; next();
            if (tk == '(') { // function call
                next(); t = 0;
                while (tk != ')') { expr(Assign); *++e = _PSH; ++t; if (tk == ',') next(); }
                next();
                if (d[Class] == Sys) { *++e = d[Val]; }
                else if (d[Class] == Fun) { *++e = _JSR; *++e = d[Val]; }
                if (t) { *++e = _ADJ; *++e = t; }
                ty = d[Type];
            }
            else if (d[Class] == Num) { *++e = _IMM; *++e = d[Val]; ty = INT_; }
            else {
                if (d[Class] == Loc) { *++e = _LEA; *++e = loc - d[Val]; }
                else if (d[Class] == Glo) { *++e = _IMM; *++e = d[Val]; }
                *++e = ((ty = d[Type]) == CHAR_) ? _LC : _LI;
            }
        }
        else if (tk == '(') {
            next();
            if (tk == Int || tk == Char) {
                t = (tk == Int) ? INT_ : CHAR_; next();
                while (tk == Mul) { next(); t = t + PTR_; }
                if (tk == ')') next();
                expr(Inc);
                ty = t;
            } else {
                expr(Assign);
                if (tk == ')') next();
            }
        }
        else if (tk == Mul) { next(); expr(Inc); if (ty > INT_) ty = ty - PTR_; *++e = (ty == CHAR_) ? _LC : _LI; }
        else if (tk == And) { next(); expr(Inc); if (*e == _LC || *e == _LI) --e; ty = ty + PTR_; }
        else if (tk == '!') { next(); expr(Inc); *++e = _PSH; *++e = _IMM; *++e = 0; *++e = _EQ; ty = INT_; }
        else if (tk == '~') { next(); expr(Inc); *++e = _PSH; *++e = _IMM; *++e = -1; *++e = _XOR; ty = INT_; }
        else if (tk == Add) { next(); expr(Inc); ty = INT_; }
        else if (tk == Sub) {
            next(); *++e = _IMM;
            if (tk == Num) { *++e = -ival; next(); } else { *++e = -1; *++e = _PSH; expr(Inc); *++e = _MUL; }
            ty = INT_;
        }
        else if (tk == Inc || tk == Dec) {
            t = tk; next(); expr(Inc);
            if (*e == _LC) { *e = _PSH; *++e = _LC; } else if (*e == _LI) { *e = _PSH; *++e = _LI; }
            *++e = _PSH; *++e = _IMM; *++e = (ty > PTR_) ? (word)sizeof(word) : (word)sizeof(char);
            *++e = (t == Inc) ? _ADD : _SUB;
            *++e = (ty == CHAR_) ? _SC : _SI;
        }
        else if (tk == Sizeof) {
            next(); if (tk == '(') next();
            ty = INT_; if (tk == Int) next(); else if (tk == Char) { next(); ty = CHAR_; }
            while (tk == Mul) { next(); ty = ty + PTR_; }
            if (tk == ')') next();
            *++e = _IMM; *++e = (ty == CHAR_) ? (word)sizeof(char) : (word)sizeof(word);
            ty = INT_;
        }

        while (tk >= lev) {
            t = ty;
            if (tk == Assign) {
                next(); if (*e == _LC || *e == _LI) *e = _PSH;
                expr(Assign); *++e = ((ty = t) == CHAR_) ? _SC : _SI;
            }
            else if (tk == Cond) {
                next(); *++e = _BZ; d = ++e;
                expr(Assign);
                if (tk == ':') next();
                *d = (word)(e + 3); *++e = _JMP; d = ++e;
                expr(Cond);
                *d = (word)(e + 1);
            }
            else if (tk == Lor) { next(); *++e = _BNZ; d = ++e; expr(Lan); *d = (word)(e+1); ty = INT_; }
            else if (tk == Lan) { next(); *++e = _BZ;  d = ++e; expr(Or);  *d = (word)(e+1); ty = INT_; }
            else if (tk == Or)  { next(); *++e = _PSH; expr(Xor); *++e = _OR;  ty = INT_; }
            else if (tk == Xor) { next(); *++e = _PSH; expr(And); *++e = _XOR; ty = INT_; }
            else if (tk == And) { next(); *++e = _PSH; expr(Eq);  *++e = _AND; ty = INT_; }
            else if (tk == Eq)  { next(); *++e = _PSH; expr(Lt);  *++e = _EQ;  ty = INT_; }
            else if (tk == Ne)  { next(); *++e = _PSH; expr(Lt);  *++e = _NE;  ty = INT_; }
            else if (tk == Lt)  { next(); *++e = _PSH; expr(Shl); *++e = _LT;  ty = INT_; }
            else if (tk == Gt)  { next(); *++e = _PSH; expr(Shl); *++e = _GT;  ty = INT_; }
            else if (tk == Le)  { next(); *++e = _PSH; expr(Shl); *++e = _LE;  ty = INT_; }
            else if (tk == Ge)  { next(); *++e = _PSH; expr(Shl); *++e = _GE;  ty = INT_; }
            else if (tk == Shl) { next(); *++e = _PSH; expr(Add); *++e = _SHL; ty = INT_; }
            else if (tk == Shr) { next(); *++e = _PSH; expr(Add); *++e = _SHR; ty = INT_; }
            else if (tk == Add) {
                next(); *++e = _PSH; expr(Mul);
                if ((ty = t) > PTR_) { *++e = _PSH; *++e = _IMM; *++e = (word)sizeof(word); *++e = _MUL; }
                *++e = _ADD;
            }
            else if (tk == Sub) {
                next(); *++e = _PSH; expr(Mul);
                if (t > PTR_ && t == ty) { *++e = _SUB; *++e = _PSH; *++e = _IMM; *++e = (word)sizeof(word); *++e = _DIV; ty = INT_; }
                else if ((ty = t) > PTR_) { *++e = _PSH; *++e = _IMM; *++e = (word)sizeof(word); *++e = _MUL; *++e = _SUB; }
                else *++e = _SUB;
            }
            else if (tk == Mul) { next(); *++e = _PSH; expr(Inc); *++e = _MUL; ty = INT_; }
            else if (tk == Div) { next(); *++e = _PSH; expr(Inc); *++e = _DIV; ty = INT_; }
            else if (tk == Mod) { next(); *++e = _PSH; expr(Inc); *++e = _MOD; ty = INT_; }
            else if (tk == Inc || tk == Dec) {
                if (*e == _LC) { *e = _PSH; *++e = _LC; } else if (*e == _LI) { *e = _PSH; *++e = _LI; }
                *++e = _PSH; *++e = _IMM; *++e = (ty > PTR_) ? (word)sizeof(word) : (word)sizeof(char);
                *++e = (tk == Inc) ? _ADD : _SUB;
                *++e = (ty == CHAR_) ? _SC : _SI;
                *++e = _PSH; *++e = _IMM; *++e = (ty > PTR_) ? (word)sizeof(word) : (word)sizeof(char);
                *++e = (tk == Inc) ? _SUB : _ADD;
                next();
            }
            else if (tk == Brak) {
                next(); *++e = _PSH; expr(Assign);
                if (tk == ']') next();
                if (t > PTR_) { *++e = _PSH; *++e = _IMM; *++e = (word)sizeof(word); *++e = _MUL; }
                *++e = _ADD;
                *++e = ((ty = t - PTR_) == CHAR_) ? _LC : _LI;
            }
            else break;
        }
    }

    void stmt() {
        word *a, *b;
        if (tk == If) {
            next();
            if (tk == '(') next();
            expr(Assign);
            if (tk == ')') next();
            *++e = _BZ; b = ++e;
            stmt();
            if (tk == Else) {
                *b = (word)(e + 3); *++e = _JMP; b = ++e;
                next(); stmt();
            }
            *b = (word)(e + 1);
        }
        else if (tk == While) {
            next(); a = e + 1;
            if (tk == '(') next();
            expr(Assign);
            if (tk == ')') next();
            *++e = _BZ; b = ++e;
            stmt();
            *++e = _JMP; *++e = (word)a;
            *b = (word)(e + 1);
        }
        else if (tk == Return) {
            next();
            if (tk != ';') expr(Assign);
            *++e = _LEV;
            if (tk == ';') next();
        }
        else if (tk == '{') {
            next();
            while (tk != '}') stmt();
            next();
        }
        else if (tk == ';') { next(); }
        else { expr(Assign); if (tk == ';') next(); }
    }

public:
    std::vector<std::string> compile(const std::string &src) {
        symBuf.assign(65536, 0);
        codeBuf.assign(65536, 0);
        dataBuf.assign(65536, 0);
        functions.clear();

        sym = symBuf.data();
        e = codeBuf.data();
        data = dataBuf.data();
        p = src.c_str();
        line = 1; tk = 0; loc = 0;

        // Install keywords
        const char *kw = "char else enum if int return sizeof while "
                         "open read close printf malloc free memset memcmp exit";
        word i = Char;
        p = kw;
        while (i <= While) { next(); id[Tk] = i++; }
        // system calls
        i = _OPEN;
        while (i <= _EXIT) { next(); id[Class] = Sys; id[Type] = INT_; id[Val] = i++; }

        // Parse source
        p = src.c_str();
        line = 1; tk = 0;
        next();

        word bt, ty2;
        while (tk) {
            bt = INT_;
            if (tk == Int) next();
            else if (tk == Char) { next(); bt = CHAR_; }
            else if (tk == Enum) {
                next();
                if (tk != '{') next();
                if (tk == '{') {
                    next(); i = 0;
                    while (tk != '}') {
                        if (tk == Id) { next(); }
                        if (tk == Assign) { next(); if (tk == Num) { i = ival; next(); } }
                        id[Class] = Num; id[Type] = INT_; id[Val] = i++;
                        if (tk == ',') next();
                    }
                    next();
                }
            }
            while (tk != ';' && tk != '}') {
                ty2 = bt;
                while (tk == Mul) { next(); ty2 = ty2 + PTR_; }
                if (tk != Id) break;
                word *funcId = id;
                next();
                funcId[Type] = ty2;
                if (tk == '(') { // function
                    funcId[Class] = Fun;
                    funcId[Val] = (word)(e + 1);
                    next(); i = 0;
                    while (tk != ')') {
                        ty2 = INT_;
                        if (tk == Int) next(); else if (tk == Char) { next(); ty2 = CHAR_; }
                        while (tk == Mul) { next(); ty2 = ty2 + PTR_; }
                        if (tk == Id) {
                            id[HClass] = id[Class]; id[Class] = Loc;
                            id[HType]  = id[Type];  id[Type] = ty2;
                            id[HVal]   = id[Val];   id[Val] = i++;
                            next();
                        }
                        if (tk == ',') next();
                    }
                    next();
                    if (tk != '{') break;
                    loc = ++i;
                    next();
                    while (tk == Int || tk == Char) {
                        word lbt = (tk == Int) ? INT_ : CHAR_;
                        next();
                        while (tk != ';') {
                            ty2 = lbt;
                            while (tk == Mul) { next(); ty2 = ty2 + PTR_; }
                            if (tk == Id) {
                                id[HClass] = id[Class]; id[Class] = Loc;
                                id[HType]  = id[Type];  id[Type] = ty2;
                                id[HVal]   = id[Val];   id[Val] = ++i;
                                next();
                            }
                            if (tk == ',') next();
                        }
                        next();
                    }
                    *++e = _ENT; *++e = i - loc;
                    while (tk != '}') stmt();
                    *++e = _LEV;
                    // Save function variable info before unwinding
                    {
                        FuncInfo fi;
                        fi.name = std::string((const char *)funcId[Name], p - (const char *)funcId[Name]);
                        // extract just the identifier
                        { auto sp = fi.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789");
                          if (sp != std::string::npos) fi.name = fi.name.substr(0, sp); }
                        fi.entry = (word *)funcId[Val];
                        fi.end = e + 1; // one past the final _LEV
                        fi.loc = (int)loc;
                        word *sid = sym;
                        while (sid[Tk]) {
                            if (sid[Class] == Loc) {
                                VarInfo vi;
                                // Extract name from symbol Name pointer
                                const char *np = (const char *)sid[Name];
                                const char *ne = np;
                                while (*ne && ((*ne >= 'a' && *ne <= 'z') || (*ne >= 'A' && *ne <= 'Z') || (*ne >= '0' && *ne <= '9') || *ne == '_')) ++ne;
                                vi.name = std::string(np, ne - np);
                                vi.leaOffset = (int)(loc - sid[Val]);
                                vi.isParam = (sid[Val] < loc);
                                fi.vars.push_back(vi);
                            }
                            sid = sid + Idsz;
                        }
                        functions.push_back(fi);
                    }
                    // unwind locals
                    id = sym;
                    while (id[Tk]) {
                        if (id[Class] == Loc) {
                            id[Class] = id[HClass]; id[Type] = id[HType]; id[Val] = id[HVal];
                        }
                        id = id + Idsz;
                    }
                } else {
                    funcId[Class] = Glo;
                    funcId[Val] = (word)data;
                    data = data + sizeof(word);
                }
                if (tk == ',') next();
            }
            next();
        }

        // Convert code buffer to display strings
        word *codeBase = codeBuf.data();
        word *codeEnd = e;
        std::vector<std::string> result;

        // First pass: map buffer positions to instruction indices
        std::vector<int> bufPosToAddr(codeEnd - codeBase + 2, -1);
        bufPosToLine.assign(codeEnd - codeBase + 2, -1);
        {
            word *pc = codeBase + 1;
            int idx = 0;
            while (pc <= codeEnd) {
                int pos = (int)(pc - codeBase);
                bufPosToAddr[pos] = idx;
                bufPosToLine[pos] = idx;
                ++idx;
                word op = *pc++;
                if (op >= 0 && op <= _ADJ) ++pc; // skip operand
            }
        }

        // Second pass: generate display strings
        word *pc = codeBase + 1;
        int addr = 0;
        while (pc <= codeEnd) {
            word op = *pc++;
            char buf[64];
            if (op < 0 || op > _EXIT) {
                snprintf(buf, sizeof(buf), "%4d ???  %lld", addr++, (long long)op);
                result.push_back(buf);
                continue;
            }
            if (op <= _ADJ) { // instructions with operand
                word arg = *pc++;
                if (op == _JMP || op == _JSR || op == _BZ || op == _BNZ) {
                    int targetBufPos = (int)((word *)arg - codeBase);
                    int targetAddr = (targetBufPos >= 0 && targetBufPos < (int)bufPosToAddr.size())
                                     ? bufPosToAddr[targetBufPos] : -1;
                    snprintf(buf, sizeof(buf), "%4d %-4s %d", addr, c4_opnames[op], targetAddr);
                } else {
                    snprintf(buf, sizeof(buf), "%4d %-4s %lld", addr, c4_opnames[op], (long long)arg);
                }
            } else {
                snprintf(buf, sizeof(buf), "%4d %-4s", addr, c4_opnames[op]);
            }
            result.push_back(buf);
            ++addr;
        }
        return result;
    }

    // --- VM state for step-by-step execution ---
    word *vmPC, *vmSP, *vmBP;
    word vmA;
    int vmCycle;
    int vmCallDepth;  // call depth (incremented by JSR/ENT, decremented by LEV)
    bool vmRunning;
    std::vector<word> vmStack;
    std::vector<int> bufPosToLine; // codeBuf position → display line index

    // Initialize VM for execution (call after compile)
    bool initVM() {
        // Find main() — last Fun symbol
        word *mainId = nullptr;
        word *idm = sym;
        while (idm[Tk]) {
            if (idm[Class] == Fun) mainId = idm;
            idm = idm + Idsz;
        }
        if (!mainId) return false;

        vmStack.assign(65536, 0);
        vmSP = vmStack.data() + vmStack.size();
        vmBP = vmSP;
        vmA = 0;
        vmCycle = 0;
        vmCallDepth = 0;
        vmRunning = true;

        // Setup stack for main()
        *--vmSP = _EXIT;
        *--vmSP = _PSH;
        word *t = vmSP;
        *--vmSP = 0; // argc
        *--vmSP = 0; // argv
        *--vmSP = (word)t;

        vmPC = (word *)mainId[Val];
        return true;
    }

    // Get display line index for current PC position
    int currentDisplayLine() const {
        int pos = (int)(vmPC - codeBuf.data());
        if (pos >= 0 && pos < (int)bufPosToLine.size())
            return bufPosToLine[pos];
        return -1;
    }

    // Get stack snapshot: returns (index, value) pairs from SP to stack top
    // Also returns SP and BP indices relative to stack base
    struct StackInfo {
        struct Entry { int offset; word value; };
        std::vector<Entry> entries;
        int spIdx; // index of SP in entries (0 = top)
        int bpIdx; // index of BP in entries (-1 if outside range)
    };
    StackInfo getStackInfo() const {
        StackInfo si;
        si.spIdx = 0;
        si.bpIdx = -1;
        const word *top = vmStack.data() + vmStack.size();
        int i = 0;
        for (const word *q = vmSP; q < top; ++q, ++i) {
            si.entries.push_back({i, *q});
            if (q == vmBP) si.bpIdx = i;
        }
        return si;
    }

    // Find current function based on PC position in code
    const FuncInfo *currentFunction() const {
        for (auto &fi : functions) {
            if (vmPC >= fi.entry && vmPC < fi.end)
                return &fi;
        }
        return nullptr;
    }

    // Get current frame variables with values
    struct FrameVar {
        std::string name;
        word value;
        bool isParam;
    };
    std::vector<FrameVar> getFrameVars() const {
        std::vector<FrameVar> result;
        auto *fi = currentFunction();
        if (!fi || !vmBP) return result;
        for (auto &vi : fi->vars) {
            FrameVar fv;
            fv.name = vi.name;
            fv.isParam = vi.isParam;
            // Read value from stack frame: bp + leaOffset
            const word *addr = vmBP + vi.leaOffset;
            const word *stackBase = vmStack.data();
            const word *stackTop = vmStack.data() + vmStack.size();
            if (addr >= stackBase && addr < stackTop)
                fv.value = *addr;
            else
                fv.value = 0;
            result.push_back(fv);
        }
        return result;
    }

    // Execute one VM instruction. Returns opcode executed, or -1 if done.
    // If output is produced (PRTF/EXIT), it's appended to `out`.
    int stepVM(std::string &out) {
        out.clear();
        if (!vmRunning || vmCycle >= 1000000) { vmRunning = false; return -1; }

        word i = *vmPC++; ++vmCycle;
        if      (i == _LEA) vmA = (word)(vmBP + *vmPC++);
        else if (i == _IMM) vmA = *vmPC++;
        else if (i == _JMP) vmPC = (word *)*vmPC;
        else if (i == _JSR) { *--vmSP = (word)(vmPC + 1); vmPC = (word *)*vmPC; }
        else if (i == _BZ)  vmPC = vmA ? vmPC + 1 : (word *)*vmPC;
        else if (i == _BNZ) vmPC = vmA ? (word *)*vmPC : vmPC + 1;
        else if (i == _ENT) { *--vmSP = (word)vmBP; vmBP = vmSP; vmSP = vmSP - *vmPC++; ++vmCallDepth; }
        else if (i == _ADJ) vmSP = vmSP + *vmPC++;
        else if (i == _LEV) { vmSP = vmBP; vmBP = (word *)*vmSP++; vmPC = (word *)*vmSP++; --vmCallDepth; }
        else if (i == _LI)  vmA = *(word *)vmA;
        else if (i == _LC)  vmA = *(char *)vmA;
        else if (i == _SI)  *(word *)*vmSP++ = vmA;
        else if (i == _SC)  vmA = *(char *)*vmSP++ = (char)vmA;
        else if (i == _PSH) *--vmSP = vmA;
        else if (i == _OR)  vmA = *vmSP++ |  vmA;
        else if (i == _XOR) vmA = *vmSP++ ^  vmA;
        else if (i == _AND) vmA = *vmSP++ &  vmA;
        else if (i == _EQ)  vmA = *vmSP++ == vmA;
        else if (i == _NE)  vmA = *vmSP++ != vmA;
        else if (i == _LT)  vmA = *vmSP++ <  vmA;
        else if (i == _GT)  vmA = *vmSP++ >  vmA;
        else if (i == _LE)  vmA = *vmSP++ <= vmA;
        else if (i == _GE)  vmA = *vmSP++ >= vmA;
        else if (i == _SHL) vmA = *vmSP++ << vmA;
        else if (i == _SHR) vmA = *vmSP++ >> vmA;
        else if (i == _ADD) vmA = *vmSP++ +  vmA;
        else if (i == _SUB) vmA = *vmSP++ -  vmA;
        else if (i == _MUL) vmA = *vmSP++ *  vmA;
        else if (i == _DIV) vmA = *vmSP++ /  vmA;
        else if (i == _MOD) vmA = *vmSP++ %  vmA;
        else if (i == _PRTF) {
            word *t = vmSP + vmPC[1];
            char buf[256];
            snprintf(buf, sizeof(buf), (char *)t[-1], t[-2], t[-3], t[-4], t[-5], t[-6]);
            // Strip trailing newline for display
            std::string s = buf;
            while (!s.empty() && s.back() == '\n') s.pop_back();
            out = s;
        }
        else if (i == _EXIT) {
            char buf[64];
            snprintf(buf, sizeof(buf), "exit(%lld)", (long long)*vmSP);
            out = buf;
            vmRunning = false;
        }
        else { vmRunning = false; return -1; }
        return (int)i;
    }
};

/////////////////////////////////////////////////////////////////////////
// TextViewer - A simple view that displays lines of text

class TextViewer : public TView
{
public:
    TextViewer(const TRect &bounds) noexcept;

    void draw() override;
    void handleEvent(TEvent &event) override;
    void setText(const std::vector<std::string> &lines);
    void addLine(const std::string &line);
    void clear();
    void setScrollOffset(int x, int y);
    void scrollTo(int x, int y);
    void syncScrollBars();
    virtual int maxLineWidth() const;

    std::vector<std::string> lines;
    int scrollX {0};
    int scrollY {0};
    TColorAttr normalColor;
    TColorAttr highlightColor;
    int highlightLine {-1};

    TScrollBar *hScrollBar {nullptr};
    TScrollBar *vScrollBar {nullptr};
};

TextViewer::TextViewer(const TRect &bounds) noexcept :
    TView(bounds)
{
    growMode = gfGrowHiX | gfGrowHiY;
    options |= ofSelectable;
    eventMask |= evKeyboard | evMouseDown | evMouseWheel | evBroadcast;
    normalColor = {'\xF', '\x1'}; // white on blue
    highlightColor = {'\xE', '\x5'}; // yellow on magenta
}

void TextViewer::draw()
{
    for (int y = 0; y < size.y; ++y)
    {
        TDrawBuffer b;
        int lineIdx = y + scrollY;
        TColorAttr color = (lineIdx == highlightLine) ? highlightColor : normalColor;
        b.moveChar(0, ' ', color, size.x);
        if (lineIdx >= 0 && lineIdx < (int)lines.size())
        {
            const auto &line = lines[lineIdx];
            int startX = std::min(scrollX, (int)line.size());
            TStringView sv(line.c_str() + startX, std::min((int)line.size() - startX, size.x));
            b.moveStr(0, sv, color);
        }
        writeLine(0, y, size.x, 1, b);
    }
}

void TextViewer::setText(const std::vector<std::string> &newLines)
{
    lines = newLines;
    scrollX = 0;
    scrollY = 0;
    syncScrollBars();
    drawView();
}

void TextViewer::addLine(const std::string &line)
{
    lines.push_back(line);
    // Auto-scroll to bottom
    if ((int)lines.size() > size.y)
        scrollY = (int)lines.size() - size.y;
    syncScrollBars();
    drawView();
}

void TextViewer::clear()
{
    lines.clear();
    scrollX = 0;
    scrollY = 0;
    syncScrollBars();
    drawView();
}

void TextViewer::setScrollOffset(int x, int y)
{
    scrollX = x;
    scrollY = y;
    drawView();
}

void TextViewer::scrollTo(int x, int y)
{
    int maxY = std::max(0, (int)lines.size() - size.y);
    int maxX = std::max(0, maxLineWidth() - size.x);
    scrollY = std::max(0, std::min(y, maxY));
    scrollX = std::max(0, std::min(x, maxX));
    syncScrollBars();
    drawView();
}

void TextViewer::syncScrollBars()
{
    if (vScrollBar)
    {
        int maxY = std::max(0, (int)lines.size() - size.y);
        vScrollBar->setParams(scrollY, 0, maxY, size.y - 1, 1);
    }
    if (hScrollBar)
    {
        int maxX = std::max(0, maxLineWidth() - size.x);
        hScrollBar->setParams(scrollX, 0, maxX, size.x - 1, 1);
    }
}

int TextViewer::maxLineWidth() const
{
    int w = 0;
    for (auto &l : lines)
        w = std::max(w, (int)l.size());
    return w;
}

void TextViewer::handleEvent(TEvent &event)
{
    TView::handleEvent(event);

    if (event.what == evKeyboard)
    {
        switch (event.keyDown.keyCode)
        {
            case kbUp:    scrollTo(scrollX, scrollY - 1); break;
            case kbDown:  scrollTo(scrollX, scrollY + 1); break;
            case kbLeft:  scrollTo(scrollX - 1, scrollY); break;
            case kbRight: scrollTo(scrollX + 1, scrollY); break;
            case kbPgUp:  scrollTo(scrollX, scrollY - size.y); break;
            case kbPgDn:  scrollTo(scrollX, scrollY + size.y); break;
            case kbHome:  scrollTo(0, scrollY); break;
            case kbEnd:   scrollTo(maxLineWidth() - size.x, scrollY); break;
            case kbCtrlHome: scrollTo(scrollX, 0); break;
            case kbCtrlEnd:  scrollTo(scrollX, (int)lines.size()); break;
            default: return;
        }
        clearEvent(event);
    }
    else if (event.what == evMouseWheel)
    {
        if (event.mouse.wheel & mwDown)
            scrollTo(scrollX, scrollY + 3);
        else if (event.mouse.wheel & mwUp)
            scrollTo(scrollX, scrollY - 3);
        clearEvent(event);
    }
    else if (event.what == evBroadcast)
    {
        if (event.message.command == cmScrollBarChanged)
        {
            if (event.message.infoPtr == vScrollBar && vScrollBar)
                scrollTo(scrollX, vScrollBar->value);
            else if (event.message.infoPtr == hScrollBar && hScrollBar)
                scrollTo(hScrollBar->value, scrollY);
            else
                return;
            clearEvent(event);
        }
    }
    else if (event.what == evMouseDown)
    {
        select();
    }
}

/////////////////////////////////////////////////////////////////////////
// NumberedTextViewer - Like TextViewer but with line numbers

class NumberedTextViewer : public TextViewer
{
public:
    NumberedTextViewer(const TRect &bounds, int numWidth = 6) noexcept;
    void draw() override;
    void handleEvent(TEvent &event) override;
    int maxLineWidth() const override;

    int numWidth;
    int startLineNum {1};
    std::set<int> breakpoints; // set of line indices (0-based) with breakpoints
    bool breakpointsEnabled {false}; // only C4 window enables this
};

NumberedTextViewer::NumberedTextViewer(const TRect &bounds, int aNumWidth) noexcept :
    TextViewer(bounds),
    numWidth(aNumWidth)
{
}

void NumberedTextViewer::draw()
{
    TColorAttr numColor = {'\xE', '\x1'};    // yellow on blue
    TColorAttr bpColor  = {'\xF', '\x4'};    // white on red (breakpoint)
    TColorAttr bpNumColor = {'\xE', '\x4'};  // yellow on red

    for (int y = 0; y < size.y; ++y)
    {
        TDrawBuffer b;
        int lineIdx = y + scrollY;
        bool hasBP = breakpoints.count(lineIdx) > 0;
        TColorAttr color = (lineIdx == highlightLine) ? highlightColor
                         : hasBP ? bpColor : normalColor;
        TColorAttr nColor = hasBP ? bpNumColor : numColor;
        b.moveChar(0, ' ', color, size.x);

        if (lineIdx >= 0 && lineIdx < (int)lines.size())
        {
            // Draw line number (with breakpoint marker)
            char numBuf[16];
            if (hasBP)
                snprintf(numBuf, sizeof(numBuf), "*%*d", numWidth - 1, lineIdx + startLineNum);
            else
                snprintf(numBuf, sizeof(numBuf), "%*d ", numWidth - 1, lineIdx + startLineNum);
            b.moveStr(0, numBuf, nColor);

            // Draw text
            const auto &line = lines[lineIdx];
            int startX = std::min(scrollX, (int)line.size());
            int maxChars = size.x - numWidth;
            if (maxChars > 0)
            {
                TStringView sv(line.c_str() + startX,
                               std::min((int)line.size() - startX, maxChars));
                b.moveStr(numWidth, sv, color);
            }
        }
        writeLine(0, y, size.x, 1, b);
    }
}

int NumberedTextViewer::maxLineWidth() const
{
    // Account for line number column so scrollbar range covers all hidden text
    return TextViewer::maxLineWidth() + numWidth;
}

void NumberedTextViewer::handleEvent(TEvent &event)
{
    if (breakpointsEnabled && event.what == evMouseDown) {
        TPoint mouse = makeLocal(event.mouse.where);
        if (mouse.x < numWidth) {
            // Click on line number area — toggle breakpoint
            int lineIdx = mouse.y + scrollY;
            if (lineIdx >= 0 && lineIdx < (int)lines.size()) {
                if (breakpoints.count(lineIdx))
                    breakpoints.erase(lineIdx);
                else
                    breakpoints.insert(lineIdx);
                drawView();
            }
            clearEvent(event);
            return;
        }
    }
    TextViewer::handleEvent(event);
}

/////////////////////////////////////////////////////////////////////////
// TextWindow - A window containing a TextViewer

class TextWindow : public TWindow
{
public:
    TextWindow(const TRect &bounds, const char *title, bool numbered = false) noexcept;
    void setState(ushort aState, Boolean enable) override;

    TextViewer *viewer;
};

TextWindow::TextWindow(const TRect &bounds, const char *title, bool numbered) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, title, wnNoNumber)
{
    flags = wfMove | wfGrow | wfZoom;
    options |= ofTileable;
    state &= ~sfShadow;
    palette = wpBlueWindow;

    TRect extent = getExtent().grow(-1, -1);

    // Vertical scrollbar (right edge inside frame)
    TRect vsbR(extent.b.x, extent.a.y, extent.b.x + 1, extent.b.y);
    auto *vsb = new TScrollBar(vsbR);
    vsb->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    insert(vsb);

    // Horizontal scrollbar (bottom edge inside frame)
    TRect hsbR(extent.a.x, extent.b.y, extent.b.x, extent.b.y + 1);
    auto *hsb = new TScrollBar(hsbR);
    hsb->growMode = gfGrowLoY | gfGrowHiY;
    insert(hsb);

    // Viewer area (shrink to make room for scrollbars)
    TRect r(extent.a.x, extent.a.y, extent.b.x, extent.b.y);
    if (numbered)
        viewer = new NumberedTextViewer(r);
    else
        viewer = new TextViewer(r);
    viewer->vScrollBar = vsb;
    viewer->hScrollBar = hsb;
    insert(viewer);

    // Hide scrollbars initially (shown when window is selected)
    vsb->hide();
    hsb->hide();
}

void TextWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    if (aState == sfSelected && viewer)
    {
        if (enable)
        {
            if (viewer->vScrollBar) viewer->vScrollBar->show();
            if (viewer->hScrollBar) viewer->hScrollBar->show();
            viewer->syncScrollBars();
        }
        else
        {
            if (viewer->vScrollBar) viewer->vScrollBar->hide();
            if (viewer->hScrollBar) viewer->hScrollBar->hide();
        }
    }
}

/////////////////////////////////////////////////////////////////////////
// TableViewer - A view that displays tabular data (for Stack, Display)

class TableViewer : public TView
{
public:
    TableViewer(const TRect &bounds) noexcept;
    void draw() override;
    void handleEvent(TEvent &event) override;
    void scrollTo(int y);
    void syncScrollBar();

    struct Row {
        int index;
        int value;
        std::string label; // optional label (e.g., variable name)
    };
    std::vector<Row> rows;
    int scrollY {0};
    int marker {-1};       // highlighted row
    std::string markerLabel; // e.g., "<B", "<T"

    TScrollBar *vScrollBar {nullptr};
};

TableViewer::TableViewer(const TRect &bounds) noexcept :
    TView(bounds)
{
    growMode = gfGrowHiX | gfGrowHiY;
    options |= ofSelectable;
    eventMask |= evKeyboard | evMouseDown | evMouseWheel | evBroadcast;
}

void TableViewer::scrollTo(int y)
{
    int maxY = std::max(0, (int)rows.size() - size.y);
    scrollY = std::max(0, std::min(y, maxY));
    syncScrollBar();
    drawView();
}

void TableViewer::syncScrollBar()
{
    if (vScrollBar)
    {
        int maxY = std::max(0, (int)rows.size() - size.y);
        vScrollBar->setParams(scrollY, 0, maxY, size.y - 1, 1);
    }
}

void TableViewer::handleEvent(TEvent &event)
{
    TView::handleEvent(event);

    if (event.what == evKeyboard)
    {
        switch (event.keyDown.keyCode)
        {
            case kbUp:    scrollTo(scrollY - 1); break;
            case kbDown:  scrollTo(scrollY + 1); break;
            case kbPgUp:  scrollTo(scrollY - size.y); break;
            case kbPgDn:  scrollTo(scrollY + size.y); break;
            case kbCtrlHome: scrollTo(0); break;
            case kbCtrlEnd:  scrollTo((int)rows.size()); break;
            default: return;
        }
        clearEvent(event);
    }
    else if (event.what == evMouseWheel)
    {
        if (event.mouse.wheel & mwDown)
            scrollTo(scrollY + 3);
        else if (event.mouse.wheel & mwUp)
            scrollTo(scrollY - 3);
        clearEvent(event);
    }
    else if (event.what == evBroadcast)
    {
        if (event.message.command == cmScrollBarChanged &&
            event.message.infoPtr == vScrollBar && vScrollBar)
        {
            scrollTo(vScrollBar->value);
            clearEvent(event);
        }
    }
    else if (event.what == evMouseDown)
    {
        select();
    }
}

void TableViewer::draw()
{
    TColorAttr normal = {'\xF', '\x1'};    // white on blue
    TColorAttr markerClr = {'\xE', '\x5'}; // yellow on magenta

    for (int y = 0; y < size.y; ++y)
    {
        TDrawBuffer b;
        int rowIdx = y + scrollY;
        bool isMarked = (rowIdx == marker);
        TColorAttr color = isMarked ? markerClr : normal;
        b.moveChar(0, ' ', color, size.x);

        if (rowIdx >= 0 && rowIdx < (int)rows.size())
        {
            char buf[64];
            if (rows[rowIdx].label.empty())
                snprintf(buf, sizeof(buf), "%4d %8d", rows[rowIdx].index, rows[rowIdx].value);
            else
                snprintf(buf, sizeof(buf), "%-10s %8d", rows[rowIdx].label.c_str(), rows[rowIdx].value);
            b.moveStr(0, buf, color);
            if (isMarked && !markerLabel.empty())
            {
                TColorAttr mClr = {'\xE', '\x5'};
                b.moveStr(size.x - (int)markerLabel.size(), markerLabel.c_str(), mClr);
            }
        }
        writeLine(0, y, size.x, 1, b);
    }
}

/////////////////////////////////////////////////////////////////////////
// TableWindow - Window containing a TableViewer

class TableWindow : public TWindow
{
public:
    TableWindow(const TRect &bounds, const char *title) noexcept;
    void setState(ushort aState, Boolean enable) override;
    TableViewer *viewer;
};

TableWindow::TableWindow(const TRect &bounds, const char *title) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, title, wnNoNumber)
{
    flags = wfMove | wfGrow | wfZoom;
    options |= ofTileable;
    state &= ~sfShadow;
    palette = wpBlueWindow;

    TRect extent = getExtent().grow(-1, -1);

    TRect vsbR(extent.b.x, extent.a.y, extent.b.x + 1, extent.b.y);
    auto *vsb = new TScrollBar(vsbR);
    vsb->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    insert(vsb);

    TRect r(extent.a.x, extent.a.y, extent.b.x, extent.b.y);
    viewer = new TableViewer(r);
    viewer->vScrollBar = vsb;
    insert(viewer);

    vsb->hide();
}

void TableWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    if (aState == sfSelected && viewer && viewer->vScrollBar)
    {
        if (enable)
        {
            viewer->vScrollBar->show();
            viewer->syncScrollBar();
        }
        else
            viewer->vScrollBar->hide();
    }
}

/////////////////////////////////////////////////////////////////////////
// ProcListWindow - Window containing a TextViewer for procedure list

class ProcListWindow : public TWindow
{
public:
    ProcListWindow(const TRect &bounds) noexcept;
    void setState(ushort aState, Boolean enable) override;

    TextViewer *viewer;
    std::vector<std::string> items;

    void updateList();
};

ProcListWindow::ProcListWindow(const TRect &bounds) noexcept :
    TWindowInit(&TWindow::initFrame),
    TWindow(bounds, "Token", wnNoNumber)
{
    flags = wfMove | wfGrow | wfZoom;
    options |= ofTileable;
    state &= ~sfShadow;
    palette = wpBlueWindow;

    TRect extent = getExtent().grow(-1, -1);

    TRect vsbR(extent.b.x, extent.a.y, extent.b.x + 1, extent.b.y);
    auto *vsb = new TScrollBar(vsbR);
    vsb->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    insert(vsb);

    TRect r(extent.a.x, extent.a.y, extent.b.x, extent.b.y);
    viewer = new TextViewer(r);
    viewer->vScrollBar = vsb;
    insert(viewer);

    vsb->hide();
}

void ProcListWindow::setState(ushort aState, Boolean enable)
{
    TWindow::setState(aState, enable);
    if (aState == sfSelected && viewer && viewer->vScrollBar)
    {
        if (enable)
        {
            viewer->vScrollBar->show();
            viewer->syncScrollBars();
        }
        else
            viewer->vScrollBar->hide();
    }
}

void ProcListWindow::updateList()
{
    viewer->setText(items);
}

/////////////////////////////////////////////////////////////////////////
// VisiC4App - Main application

class VisiC4App : public TApplication
{
public:
    VisiC4App() noexcept;

    static TMenuBar *initMenuBar(TRect r) noexcept;
    static TStatusLine *initStatusLine(TRect r) noexcept;

    void handleEvent(TEvent &event) override;
    void idle() override;

    void createWindows();
    void tileWindows();
    void switchMode(AppMode mode);
    void removeAllWindows();
    void layoutCompileMode();
    void layoutRuntimeMode();

    void loadSourceFile();
    void doCompile();
    void compileStep();
    void doSpeedDialog();
    void clearBreakpoints();
    void continueTrace();
    void doRun();
    void runStep();
    void doStepOver();
    void doCycle();
    void doStop();
    void doRestart();

    // Compile trace
    CompileTrace trace;
    bool tracePaused {false};

    // Compiled code (gradual reveal during trace)
    C4Compiler compiler;
    std::vector<std::string> compiledCode;
    int codeRevealPos {0};

    // Run trace
    bool runTraceActive {false};

    // Windows
    TextWindow *clangWin {nullptr};      // Compiler source
    TextWindow *grammarWin {nullptr};    // Grammar/BNF rules
    ProcListWindow *procListWin {nullptr}; // Procedure list
    TextWindow *inputWin {nullptr};      // Input source file
    TextWindow *codeWin {nullptr};       // Generated code
    TableWindow *stackWin {nullptr};     // Runtime stack
    TableWindow *displayWin {nullptr};   // Display/memory
    TextWindow *ioWin {nullptr};         // Input/Output
    TextWindow *sourceWin {nullptr};     // Source (runtime mode)

    AppMode currentMode {mCompile};
    std::string inputFilePath;

    // Compile speed: delay in milliseconds between steps
    int compileDelayMs {0};            // default: fastest
    std::chrono::steady_clock::time_point lastStepTime;
};

VisiC4App::VisiC4App() noexcept :
    TProgInit(&VisiC4App::initStatusLine,
              &VisiC4App::initMenuBar,
              &TApplication::initDeskTop)
{
    createWindows();

    // C4 source (embedded)
    clangWin->viewer->setText({
        "// c4.c - C in four functions",
        "",
        "// char, int, and pointer types",
        "// if, while, return, and expression statements",
        "// just enough features to allow self-compilation and a bit more",
        "",
        "// Written by Robert Swierczek",
        "",
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <memory.h>",
        "#include <unistd.h>",
        "#include <fcntl.h>",
        "#define int long long",
        "",
        "char *p, *lp, // current position in source code",
        "     *data;   // data/bss pointer",
        "char *op_code;",
        "",
        "int *e, *le,  // current position in emitted code",
        "    *id,      // currently parsed identifier",
        "    *sym,     // symbol table (simple list of identifiers)",
        "    tk,       // current token",
        "    ival,     // current token value",
        "    ty,       // current expression type",
        "    loc,      // local variable offset",
        "    line,     // current line number",
        "    src,      // print source and assembly flag",
        "    debug;    // print executed instructions",
        "",
        "// tokens and classes (operators last and in precedence order)",
        "enum {",
        "  Num = 128, Fun, Sys, Glo, Loc, Id,",
        "  Char, Else, Enum, If, Int, Return, Sizeof, While,",
        "  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak",
        "};",
        "",
        "// opcodes",
        "enum { LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,",
        "       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,",
        "       OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT };",
        "",
        "// types",
        "enum { CHAR, INT, PTR };",
        "",
        "// identifier offsets (since we can't create an ident struct)",
        "enum { Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz };",
        "",
        "void next()",
        "{",
        "  char *pp;",
        "",
        "  while ((tk = *p)) {",
        "    ++p;",
        "    if (tk == '\\n') {",
        "      if (src) {",
        "        printf(\"%lld: %.*s\", line, (int32_t)(p - lp), lp);",
        "        lp = p;",
        "        while (le < e) {",
        "          printf(\"%8.4s\", &op_code[*++le * 5]);",
        "          if (*le <= ADJ) printf(\" %lld\\n\", *++le); else printf(\"\\n\");",
        "        }",
        "      }",
        "      ++line;",
        "    }",
        "    else if (tk == '#') {",
        "      while (*p != 0 && *p != '\\n') ++p;",
        "    }",
        "    else if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') || tk == '_') {",
        "      pp = p - 1;",
        "      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')",
        "        tk = tk * 147 + *p++;",
        "      tk = (tk << 6) + (p - pp); // id[Hash] & 0x3f ;gives length of identifier",
        "      id = sym;",
        "      while (id[Tk]) {",
        "        if (tk == id[Hash] && !memcmp((char *)id[Name], pp, p - pp)) { tk = id[Tk]; return; }",
        "        id = id + Idsz;",
        "      }",
        "      id[Name] = (int)pp;",
        "      id[Hash] = tk;",
        "      tk = id[Tk] = Id;",
        "      return;",
        "    }",
        "    else if (tk >= '0' && tk <= '9') {",
        "      if ((ival = tk - '0')) { while (*p >= '0' && *p <= '9') ival = ival * 10 + *p++ - '0'; }",
        "      else if (*p == 'x' || *p == 'X') {",
        "        while ((tk = *++p) && ((tk >= '0' && tk <= '9') || (tk >= 'a' && tk <= 'f') || (tk >= 'A' && tk <= 'F')))",
        "          ival = ival * 16 + (tk & 15) + (tk >= 'A' ? 9 : 0);",
        "      }",
        "      else { while (*p >= '0' && *p <= '7') ival = ival * 8 + *p++ - '0'; }",
        "      tk = Num;",
        "      return;",
        "    }",
        "    else if (tk == '/') {",
        "      if (*p == '/') {",
        "        ++p;",
        "        while (*p != 0 && *p != '\\n') ++p;",
        "      }",
        "      else {",
        "        tk = Div;",
        "        return;",
        "      }",
        "    }",
        "    else if (tk == '\\'' || tk == '\"') {",
        "      pp = data;",
        "      while (*p != 0 && *p != tk) {",
        "        if ((ival = *p++) == '\\\\') {",
        "          if ((ival = *p++) == 'n') ival = '\\n';",
        "        }",
        "        if (tk == '\"') *data++ = ival;",
        "      }",
        "      ++p;",
        "      if (tk == '\"') ival = (int)pp; else tk = Num;",
        "      return;",
        "    }",
        "    else if (tk == '=') { if (*p == '=') { ++p; tk = Eq; } else tk = Assign; return; }",
        "    else if (tk == '+') { if (*p == '+') { ++p; tk = Inc; } else tk = Add; return; }",
        "    else if (tk == '-') { if (*p == '-') { ++p; tk = Dec; } else tk = Sub; return; }",
        "    else if (tk == '!') { if (*p == '=') { ++p; tk = Ne; } return; }",
        "    else if (tk == '<') { if (*p == '=') { ++p; tk = Le; } else if (*p == '<') { ++p; tk = Shl; } else tk = Lt; return; }",
        "    else if (tk == '>') { if (*p == '=') { ++p; tk = Ge; } else if (*p == '>') { ++p; tk = Shr; } else tk = Gt; return; }",
        "    else if (tk == '|') { if (*p == '|') { ++p; tk = Lor; } else tk = Or; return; }",
        "    else if (tk == '&') { if (*p == '&') { ++p; tk = Lan; } else tk = And; return; }",
        "    else if (tk == '^') { tk = Xor; return; }",
        "    else if (tk == '%') { tk = Mod; return; }",
        "    else if (tk == '*') { tk = Mul; return; }",
        "    else if (tk == '[') { tk = Brak; return; }",
        "    else if (tk == '?') { tk = Cond; return; }",
        "    else if (tk == '~' || tk == ';' || tk == '{' || tk == '}' || tk == '(' || tk == ')' || tk == ']' || tk == ',' || tk == ':') return;",
        "  }",
        "}",
        "",
        "void expr(int lev)",
        "{",
        "  int t, *d;",
        "",
        "  if (!tk) { printf(\"%lld: unexpected eof in expression\\n\", line); exit(-1); }",
        "  else if (tk == Num) { *++e = IMM; *++e = ival; next(); ty = INT; }",
        "  else if (tk == '\"') {",
        "    *++e = IMM; *++e = ival; next();",
        "    while (tk == '\"') next();",
        "    data = (char *)((int)data + sizeof(int) & -sizeof(int)); ty = PTR;",
        "  }",
        "  else if (tk == Sizeof) {",
        "    next(); if (tk == '(') next(); else { printf(\"%lld: open paren expected in sizeof\\n\", line); exit(-1); }",
        "    ty = INT; if (tk == Int) next(); else if (tk == Char) { next(); ty = CHAR; }",
        "    while (tk == Mul) { next(); ty = ty + PTR; }",
        "    if (tk == ')') next(); else { printf(\"%lld: close paren expected in sizeof\\n\", line); exit(-1); }",
        "    *++e = IMM; *++e = (ty == CHAR) ? sizeof(char) : sizeof(int);",
        "    ty = INT;",
        "  }",
        "  else if (tk == Id) {",
        "    d = id; next();",
        "    if (tk == '(') {",
        "      next();",
        "      t = 0;",
        "      while (tk != ')') { expr(Assign); *++e = PSH; ++t; if (tk == ',') next(); }",
        "      next();",
        "      if (d[Class] == Sys) *++e = d[Val];",
        "      else if (d[Class] == Fun) { *++e = JSR; *++e = d[Val]; }",
        "      else { printf(\"%lld: bad function call\\n\", line); exit(-1); }",
        "      if (t) { *++e = ADJ; *++e = t; }",
        "      ty = d[Type];",
        "    }",
        "    else if (d[Class] == Num) { *++e = IMM; *++e = d[Val]; ty = INT; }",
        "    else {",
        "      if (d[Class] == Loc) { *++e = LEA; *++e = loc - d[Val]; }",
        "      else if (d[Class] == Glo) { *++e = IMM; *++e = d[Val]; }",
        "      else { printf(\"%lld: undefined variable '%.*s'\\n\", line, (int32_t)d[Hash] & 0x3f, (char *)d[Name]); exit(-1); }",
        "      *++e = ((ty = d[Type]) == CHAR) ? LC : LI;",
        "    }",
        "  }",
        "  else if (tk == '(') {",
        "    next();",
        "    if (tk == Int || tk == Char) {",
        "      t = (tk == Int) ? INT : CHAR; next();",
        "      while (tk == Mul) { next(); t = t + PTR; }",
        "      if (tk == ')') next(); else { printf(\"%lld: bad cast\\n\", line); exit(-1); }",
        "      expr(Inc);",
        "      ty = t;",
        "    }",
        "    else {",
        "      expr(Assign);",
        "      if (tk == ')') next(); else { printf(\"%lld: close paren expected\\n\", line); exit(-1); }",
        "    }",
        "  }",
        "  else if (tk == Mul) {",
        "    next(); expr(Inc);",
        "    if (ty > INT) ty = ty - PTR; else { printf(\"%lld: bad dereference\\n\", line); exit(-1); }",
        "    *++e = (ty == CHAR) ? LC : LI;",
        "  }",
        "  else if (tk == And) {",
        "    next(); expr(Inc);",
        "    if (*e == LC || *e == LI) --e; else { printf(\"%lld: bad address-of\\n\", line); exit(-1); }",
        "    ty = ty + PTR;",
        "  }",
        "  else if (tk == '!') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = 0; *++e = EQ; ty = INT; }",
        "  else if (tk == '~') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = -1; *++e = XOR; ty = INT; }",
        "  else if (tk == Add) { next(); expr(Inc); ty = INT; }",
        "  else if (tk == Sub) {",
        "    next(); *++e = IMM;",
        "    if (tk == Num) { *++e = -ival; next(); } else { *++e = -1; *++e = PSH; expr(Inc); *++e = MUL; }",
        "    ty = INT;",
        "  }",
        "  else if (tk == Inc || tk == Dec) {",
        "    t = tk; next(); expr(Inc);",
        "    if (*e == LC) { *e = PSH; *++e = LC; }",
        "    else if (*e == LI) { *e = PSH; *++e = LI; }",
        "    else { printf(\"%lld: bad lvalue in pre-increment\\n\", line); exit(-1); }",
        "    *++e = PSH;",
        "    *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);",
        "    *++e = (t == Inc) ? ADD : SUB;",
        "    *++e = (ty == CHAR) ? SC : SI;",
        "  }",
        "  else { printf(\"%lld: bad expression\\n\", line); exit(-1); }",
        "",
        "  while (tk >= lev) { // \"precedence climbing\" or \"Top Down Operator Precedence\" method",
        "    t = ty;",
        "    if (tk == Assign) {",
        "      next();",
        "      if (*e == LC || *e == LI) *e = PSH; else { printf(\"%lld: bad lvalue in assignment\\n\", line); exit(-1); }",
        "      expr(Assign); *++e = ((ty = t) == CHAR) ? SC : SI;",
        "    }",
        "    else if (tk == Cond) {",
        "      next();",
        "      *++e = BZ; d = ++e;",
        "      expr(Assign);",
        "      if (tk == ':') next(); else { printf(\"%lld: conditional missing colon\\n\", line); exit(-1); }",
        "      *d = (int)(e + 3); *++e = JMP; d = ++e;",
        "      expr(Cond);",
        "      *d = (int)(e + 1);",
        "    }",
        "    else if (tk == Lor) { next(); *++e = BNZ; d = ++e; expr(Lan); *d = (int)(e + 1); ty = INT; }",
        "    else if (tk == Lan) { next(); *++e = BZ;  d = ++e; expr(Or);  *d = (int)(e + 1); ty = INT; }",
        "    else if (tk == Or)  { next(); *++e = PSH; expr(Xor); *++e = OR;  ty = INT; }",
        "    else if (tk == Xor) { next(); *++e = PSH; expr(And); *++e = XOR; ty = INT; }",
        "    else if (tk == And) { next(); *++e = PSH; expr(Eq);  *++e = AND; ty = INT; }",
        "    else if (tk == Eq)  { next(); *++e = PSH; expr(Lt);  *++e = EQ;  ty = INT; }",
        "    else if (tk == Ne)  { next(); *++e = PSH; expr(Lt);  *++e = NE;  ty = INT; }",
        "    else if (tk == Lt)  { next(); *++e = PSH; expr(Shl); *++e = LT;  ty = INT; }",
        "    else if (tk == Gt)  { next(); *++e = PSH; expr(Shl); *++e = GT;  ty = INT; }",
        "    else if (tk == Le)  { next(); *++e = PSH; expr(Shl); *++e = LE;  ty = INT; }",
        "    else if (tk == Ge)  { next(); *++e = PSH; expr(Shl); *++e = GE;  ty = INT; }",
        "    else if (tk == Shl) { next(); *++e = PSH; expr(Add); *++e = SHL; ty = INT; }",
        "    else if (tk == Shr) { next(); *++e = PSH; expr(Add); *++e = SHR; ty = INT; }",
        "    else if (tk == Add) {",
        "      next(); *++e = PSH; expr(Mul);",
        "      if ((ty = t) > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL;  }",
        "      *++e = ADD;",
        "    }",
        "    else if (tk == Sub) {",
        "      next(); *++e = PSH; expr(Mul);",
        "      if (t > PTR && t == ty) { *++e = SUB; *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = DIV; ty = INT; }",
        "      else if ((ty = t) > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL; *++e = SUB; }",
        "      else *++e = SUB;",
        "    }",
        "    else if (tk == Mul) { next(); *++e = PSH; expr(Inc); *++e = MUL; ty = INT; }",
        "    else if (tk == Div) { next(); *++e = PSH; expr(Inc); *++e = DIV; ty = INT; }",
        "    else if (tk == Mod) { next(); *++e = PSH; expr(Inc); *++e = MOD; ty = INT; }",
        "    else if (tk == Inc || tk == Dec) {",
        "      if (*e == LC) { *e = PSH; *++e = LC; }",
        "      else if (*e == LI) { *e = PSH; *++e = LI; }",
        "      else { printf(\"%lld: bad lvalue in post-increment\\n\", line); exit(-1); }",
        "      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);",
        "      *++e = (tk == Inc) ? ADD : SUB;",
        "      *++e = (ty == CHAR) ? SC : SI;",
        "      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);",
        "      *++e = (tk == Inc) ? SUB : ADD;",
        "      next();",
        "    }",
        "    else if (tk == Brak) {",
        "      next(); *++e = PSH; expr(Assign);",
        "      if (tk == ']') next(); else { printf(\"%lld: close bracket expected\\n\", line); exit(-1); }",
        "      if (t > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL;  }",
        "      else if (t < PTR) { printf(\"%lld: pointer type expected\\n\", line); exit(-1); }",
        "      *++e = ADD;",
        "      *++e = ((ty = t - PTR) == CHAR) ? LC : LI;",
        "    }",
        "    else { printf(\"%lld: compiler error tk=%lld\\n\", line, tk); exit(-1); }",
        "  }",
        "}",
        "",
        "void stmt()",
        "{",
        "  int *a, *b;",
        "",
        "  if (tk == If) {",
        "    next();",
        "    if (tk == '(') next(); else { printf(\"%lld: open paren expected\\n\", line); exit(-1); }",
        "    expr(Assign);",
        "    if (tk == ')') next(); else { printf(\"%lld: close paren expected\\n\", line); exit(-1); }",
        "    *++e = BZ; b = ++e;",
        "    stmt();",
        "    if (tk == Else) {",
        "      *b = (int)(e + 3); *++e = JMP; b = ++e;",
        "      next();",
        "      stmt();",
        "    }",
        "    *b = (int)(e + 1);",
        "  }",
        "  else if (tk == While) {",
        "    next();",
        "    a = e + 1;",
        "    if (tk == '(') next(); else { printf(\"%lld: open paren expected\\n\", line); exit(-1); }",
        "    expr(Assign);",
        "    if (tk == ')') next(); else { printf(\"%lld: close paren expected\\n\", line); exit(-1); }",
        "    *++e = BZ; b = ++e;",
        "    stmt();",
        "    *++e = JMP; *++e = (int)a;",
        "    *b = (int)(e + 1);",
        "  }",
        "  else if (tk == Return) {",
        "    next();",
        "    if (tk != ';') expr(Assign);",
        "    *++e = LEV;",
        "    if (tk == ';') next(); else { printf(\"%lld: semicolon expected\\n\", line); exit(-1); }",
        "  }",
        "  else if (tk == '{') {",
        "    next();",
        "    while (tk != '}') stmt();",
        "    next();",
        "  }",
        "  else if (tk == ';') {",
        "    next();",
        "  }",
        "  else {",
        "    expr(Assign);",
        "    if (tk == ';') next(); else { printf(\"%lld: semicolon expected\\n\", line); exit(-1); }",
        "  }",
        "}",
        "",
        "int32_t main(int32_t argc, char **argv)",
        "{",
        "  int fd, bt, ty, poolsz, *idmain;",
        "  int *pc, *sp, *bp, a, cycle; // vm registers",
        "  int i, *t; // temps",
        "",
        "  --argc; ++argv;",
        "  if (argc > 0 && **argv == '-' && (*argv)[1] == 's') { src = 1; --argc; ++argv; }",
        "  if (argc > 0 && **argv == '-' && (*argv)[1] == 'd') { debug = 1; --argc; ++argv; }",
        "  if (argc < 1) { printf(\"usage: c4 [-s] [-d] file ...\\n\"); return -1; }",
        "",
        "  if ((fd = open(*argv, 0)) < 0) { printf(\"could not open(%s)\\n\", *argv); return -1; }",
        "",
        "  poolsz = 256*1024; // arbitrary size",
        "  if (!(sym = malloc(poolsz))) { printf(\"could not malloc(%lld) symbol area\\n\", poolsz); return -1; }",
        "  if (!(le = e = malloc(poolsz))) { printf(\"could not malloc(%lld) text area\\n\", poolsz); return -1; }",
        "  if (!(data = malloc(poolsz))) { printf(\"could not malloc(%lld) data area\\n\", poolsz); return -1; }",
        "  if (!(sp = malloc(poolsz))) { printf(\"could not malloc(%lld) stack area\\n\", poolsz); return -1; }",
        "",
        "  memset(sym,  0, poolsz);",
        "  memset(e,    0, poolsz);",
        "  memset(data, 0, poolsz);",
        "",
        "  p = \"char else enum if int return sizeof while \"",
        "      \"open read close printf malloc free memset memcmp exit int32_t int64_t void main\";",
        "  i = Char; while (i <= While) { next(); id[Tk] = i++; } // add keywords to symbol table",
        "  i = OPEN; while (i <= EXIT) { next(); id[Class] = Sys; id[Type] = INT; id[Val] = i++; } // add library to symbol table",
        "  next(); id[Tk] = Int;  // handle int32_t type",
        "  next(); id[Tk] = Int;  // handle int64_t type",
        "  next(); id[Tk] = Char; // handle void type",
        "  next(); idmain = id;   // keep track of main",
        "",
        "  if (!(lp = p = malloc(poolsz))) { printf(\"could not malloc(%lld) source area\\n\", poolsz); return -1; }",
        "  if ((i = read(fd, p, poolsz-1)) <= 0) { printf(\"read() returned %lld\\n\", i); return -1; }",
        "  p[i] = 0;",
        "  close(fd);",
        "",
        "  op_code = \"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,\"",
        "            \"OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,\"",
        "            \"OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,\";",
        "",
        "  // parse declarations",
        "  line = 1;",
        "  next();",
        "  while (tk) {",
        "    bt = INT; // basetype",
        "    if (tk == Int) next();",
        "    else if (tk == Char) { next(); bt = CHAR; }",
        "    else if (tk == Enum) {",
        "      next();",
        "      if (tk != '{') next();",
        "      if (tk == '{') {",
        "        next();",
        "        i = 0;",
        "        while (tk != '}') {",
        "          if (tk != Id) { printf(\"%lld: bad enum identifier %lld\\n\", line, tk); return -1; }",
        "          next();",
        "          if (tk == Assign) {",
        "            next();",
        "            if (tk != Num) { printf(\"%lld: bad enum initializer\\n\", line); return -1; }",
        "            i = ival;",
        "            next();",
        "          }",
        "          id[Class] = Num; id[Type] = INT; id[Val] = i++;",
        "          if (tk == ',') next();",
        "        }",
        "        next();",
        "      }",
        "    }",
        "    while (tk != ';' && tk != '}') {",
        "      ty = bt;",
        "      while (tk == Mul) { next(); ty = ty + PTR; }",
        "      if (tk != Id) { printf(\"%lld: bad global declaration\\n\", line); return -1; }",
        "      if (id[Class]) { printf(\"%lld: duplicate global definition\\n\", line); return -1; }",
        "      next();",
        "      id[Type] = ty;",
        "      if (tk == '(') { // function",
        "        id[Class] = Fun;",
        "        id[Val] = (int)(e + 1);",
        "        next(); i = 0;",
        "        while (tk != ')') {",
        "          ty = INT;",
        "          if (tk == Int) next();",
        "          else if (tk == Char) { next(); ty = CHAR; }",
        "          while (tk == Mul) { next(); ty = ty + PTR; }",
        "          if (tk != Id) { printf(\"%lld: bad parameter declaration\\n\", line); return -1; }",
        "          if (id[Class] == Loc) { printf(\"%lld: duplicate parameter definition\\n\", line); return -1; }",
        "          id[HClass] = id[Class]; id[Class] = Loc;",
        "          id[HType]  = id[Type];  id[Type] = ty;",
        "          id[HVal]   = id[Val];   id[Val] = i++;",
        "          next();",
        "          if (tk == ',') next();",
        "        }",
        "        next();",
        "        if (tk != '{') { printf(\"%lld: bad function definition\\n\", line); return -1; }",
        "        loc = ++i;",
        "        next();",
        "        while (tk == Int || tk == Char) {",
        "          bt = (tk == Int) ? INT : CHAR;",
        "          next();",
        "          while (tk != ';') {",
        "            ty = bt;",
        "            while (tk == Mul) { next(); ty = ty + PTR; }",
        "            if (tk != Id) { printf(\"%lld: bad local declaration\\n\", line); return -1; }",
        "            if (id[Class] == Loc) { printf(\"%lld: duplicate local definition\\n\", line); return -1; }",
        "            id[HClass] = id[Class]; id[Class] = Loc;",
        "            id[HType]  = id[Type];  id[Type] = ty;",
        "            id[HVal]   = id[Val];   id[Val] = ++i;",
        "            next();",
        "            if (tk == ',') next();",
        "          }",
        "          next();",
        "        }",
        "        *++e = ENT; *++e = i - loc;",
        "        while (tk != '}') stmt();",
        "        *++e = LEV;",
        "        id = sym; // unwind symbol table locals",
        "        while (id[Tk]) {",
        "          if (id[Class] == Loc) {",
        "            id[Class] = id[HClass];",
        "            id[Type] = id[HType];",
        "            id[Val] = id[HVal];",
        "          }",
        "          id = id + Idsz;",
        "        }",
        "      }",
        "      else {",
        "        id[Class] = Glo;",
        "        id[Val] = (int)data;",
        "        data = data + sizeof(int);",
        "      }",
        "      if (tk == ',') next();",
        "    }",
        "    next();",
        "  }",
        "",
        "  if (!(pc = (int *)idmain[Val])) { printf(\"main() not defined\\n\"); return -1; }",
        "  if (src) return 0;",
        "",
        "  // setup stack",
        "  bp = sp = (int *)((int)sp + poolsz);",
        "  *--sp = EXIT; // call exit if main returns",
        "  *--sp = PSH; t = sp;",
        "  *--sp = argc;",
        "  *--sp = (int)argv;",
        "  *--sp = (int)t;",
        "",
        "  // run...",
        "  cycle = 0;",
        "  while (1) {",
        "    i = *pc++; ++cycle;",
        "    if (debug) {",
        "      printf(\"%lld> %.4s\", cycle, &op_code[i * 5]);",
        "      if (i <= ADJ) printf(\" %lld\\n\", *pc); else printf(\"\\n\");",
        "    }",
        "    if      (i == LEA) a = (int)(bp + *pc++);                             // load local address",
        "    else if (i == IMM) a = *pc++;                                         // load global address or immediate",
        "    else if (i == JMP) pc = (int *)*pc;                                   // jump",
        "    else if (i == JSR) { *--sp = (int)(pc + 1); pc = (int *)*pc; }        // jump to subroutine",
        "    else if (i == BZ)  pc = a ? pc + 1 : (int *)*pc;                      // branch if zero",
        "    else if (i == BNZ) pc = a ? (int *)*pc : pc + 1;                      // branch if not zero",
        "    else if (i == ENT) { *--sp = (int)bp; bp = sp; sp = sp - *pc++; }     // enter subroutine",
        "    else if (i == ADJ) sp = sp + *pc++;                                   // stack adjust",
        "    else if (i == LEV) { sp = bp; bp = (int *)*sp++; pc = (int *)*sp++; } // leave subroutine",
        "    else if (i == LI)  { if (!a)   { printf(\"null pointer dereference (LI) cycle=%lld\\n\", cycle); return -1; } a = *(int *)a; }          // load int",
        "    else if (i == LC)  { if (!a)   { printf(\"null pointer dereference (LC) cycle=%lld\\n\", cycle); return -1; } a = *(char *)a; }         // load char",
        "    else if (i == SI)  { if (!*sp) { printf(\"null pointer dereference (SI) cycle=%lld\\n\", cycle); return -1; } *(int *)*sp++ = a; }      // store int",
        "    else if (i == SC)  { if (!*sp) { printf(\"null pointer dereference (SC) cycle=%lld\\n\", cycle); return -1; } a = *(char *)*sp++ = a; } // store char",
        "    else if (i == PSH) *--sp = a;                                         // push",
        "",
        "    else if (i == OR)  a = *sp++ |  a;",
        "    else if (i == XOR) a = *sp++ ^  a;",
        "    else if (i == AND) a = *sp++ &  a;",
        "    else if (i == EQ)  a = *sp++ == a;",
        "    else if (i == NE)  a = *sp++ != a;",
        "    else if (i == LT)  a = *sp++ <  a;",
        "    else if (i == GT)  a = *sp++ >  a;",
        "    else if (i == LE)  a = *sp++ <= a;",
        "    else if (i == GE)  a = *sp++ >= a;",
        "    else if (i == SHL) a = *sp++ << a;",
        "    else if (i == SHR) a = *sp++ >> a;",
        "    else if (i == ADD) a = *sp++ +  a;",
        "    else if (i == SUB) a = *sp++ -  a;",
        "    else if (i == MUL) a = *sp++ *  a;",
        "    else if (i == DIV) a = *sp++ /  a;",
        "    else if (i == MOD) a = *sp++ %  a;",
        "",
        "    else if (i == OPEN) a = open((char *)sp[1], *sp);",
        "    else if (i == READ) a = read(sp[2], (char *)sp[1], *sp);",
        "    else if (i == CLOS) a = close(*sp);",
        "    else if (i == PRTF) { t = sp + pc[1]; a = printf((char *)t[-1], t[-2], t[-3], t[-4], t[-5], t[-6]); }",
        "    else if (i == MALC) a = (int)malloc(*sp);",
        "    else if (i == FREE) free((void *)*sp);",
        "    else if (i == MSET) a = (int)memset((char *)sp[2], sp[1], *sp);",
        "    else if (i == MCMP) a = memcmp((char *)sp[2], (char *)sp[1], *sp);",
        "    else if (i == EXIT) { printf(\"exit(%lld) cycle = %lld\\n\", *sp, cycle); return *sp; }",
        "    else { printf(\"unknown instruction = %lld! cycle = %lld\\n\", i, cycle); return -1; }",
        "  }",
        "}",
    });


    // Set default grammar content (C4 subset of C)
    grammarWin->viewer->setText({
        "<program>     ::= { <global_decl> }",
        "<global_decl> ::= enum [ id ] '{' <enum_list> '}'",
        "                 | <type> [ '*' ] id <func_def> | <var_decl>",
        "<type>        ::= int | char | enum",
        "<func_def>    ::= '(' <params> ')' '{' <body> '}'",
        "<params>      ::= [ <type> [ '*' ] id { ',' <type> [ '*' ] id } ]",
        "<body>        ::= { <local_decl> } { <stmt> }",
        "<local_decl>  ::= ( int | char ) [ '*' ] id { ',' [ '*' ] id } ';'",
        "<stmt>        ::= if '(' <expr> ')' <stmt> [ else <stmt> ]",
        "                 | while '(' <expr> ')' <stmt>",
        "                 | return [ <expr> ] ';'",
        "                 | '{' { <stmt> } '}'",
        "                 | <expr> ';'  |  ';'",
        "<expr>        ::= <assign>",
        "<assign>      ::= <cond> [ '=' <assign> ]",
        "<cond>        ::= <lor> [ '?' <expr> ':' <cond> ]",
        "<lor>         ::= <lan> { '||' <lan> }",
        "<lan>         ::= <or>  { '&&' <or>  }",
        "<or>          ::= <xor> { '|'  <xor> }",
        "<xor>         ::= <and> { '^'  <and> }",
        "<and>         ::= <eq>  { '&'  <eq>  }",
        "<eq>          ::= <rel> { ('==' | '!=') <rel> }",
        "<rel>         ::= <shift> { ('<' | '>' | '<=' | '>=') <shift> }",
        "<shift>       ::= <add> { ('<<' | '>>') <add> }",
        "<add>         ::= <mul> { ('+' | '-') <mul> }",
        "<mul>         ::= <unary> { ('*' | '/' | '%') <unary> }",
        "<unary>       ::= [ '*' | '&' | '!' | '~' | '-' | '++' | '--' ] <postfix>",
        "<postfix>     ::= <primary> { '++' | '--' | '[' <expr> ']' | '(' <args> ')' }",
        "<primary>     ::= num | str | id | sizeof '(' <type> ')' | '(' <expr> ')'",
    });

    // Default source (fib.c embedded)
    inputWin->viewer->setText({
        "int fib(int i) {",
        "    if (i < 2) return i;",
        "    return fib(i-1) + fib(i-2);",
        "}",
        "",
        "int main() {",
        "    int n;",
        "",
        "    n = 10;",
        "    printf(\"fib(%2d) = %d\\n\", n, fib(n));",
        "    return 0;",
        "}",
    });

    // Set default procedure list (C4 functions)
    procListWin->items = {
        "next",
        "expr",
        "stmt",
        "main",
    };
    procListWin->updateList();
}

void VisiC4App::createWindows()
{
    TRect desk = deskTop->getExtent();
    int w = desk.b.x;
    int h = desk.b.y;

    // Create all windows at a default size; layoutXxxMode() will reposition.
    TRect defR(0, 0, w, h / 3);

    clangWin = new TextWindow(defR, "C4", true);
    clangWin->palette = wpBlueWindow;
    static_cast<NumberedTextViewer *>(clangWin->viewer)->breakpointsEnabled = true;

    grammarWin = new TextWindow(defR, "Grammar");

    procListWin = new ProcListWindow(defR);

    inputWin = new TextWindow(defR, "Source", true);
    inputWin->palette = wpBlueWindow;

    codeWin = new TextWindow(defR, "Code");
    codeWin->palette = wpBlueWindow;

    stackWin = new TableWindow(defR, "Stack");

    displayWin = new TableWindow(defR, "Variable");

    ioWin = new TextWindow(defR, "Input/Output");
    ioWin->palette = wpBlueWindow;

    sourceWin = new TextWindow(defR, "Source", true);

    // Start in compile mode
    layoutCompileMode();
}

void VisiC4App::removeAllWindows()
{
    // Remove all managed windows from desktop without destroying them.
    TWindow *wins[] = {
        clangWin, grammarWin, procListWin, inputWin, codeWin,
        stackWin, displayWin, ioWin, sourceWin
    };
    for (auto *w : wins)
        if (w && w->owner)
            w->owner->remove(w);
}

static void locateWindow(TView *v, int ax, int ay, int bx, int by)
{
    TRect r(ax, ay, bx, by);
    v->locate(r);
}

void VisiC4App::layoutCompileMode()
{
    TRect desk = deskTop->getExtent();
    int w = desk.b.x;
    int h = desk.b.y;

    int topH    = h * 3 / 8;
    int midY    = topH;
    int botY    = h * 5 / 8;
    int splitX  = w * 5 / 8;

    locateWindow(clangWin,    0,      0,      w,      topH);
    locateWindow(grammarWin,  0,      midY,   splitX, botY);
    locateWindow(procListWin, splitX, midY,   w,      botY);
    locateWindow(inputWin,    0,      botY,   splitX, h);
    locateWindow(codeWin,     splitX, botY,   w,      h);

    // Insert in back-to-front order (last inserted gets focus priority)
    deskTop->insert(codeWin);
    deskTop->insert(inputWin);
    deskTop->insert(procListWin);
    deskTop->insert(grammarWin);
    deskTop->insert(clangWin);
}

void VisiC4App::layoutRuntimeMode()
{
    TRect desk = deskTop->getExtent();
    int w = desk.b.x;
    int h = desk.b.y;

    int topH = h / 3;
    int midY = topH;
    int botY = h * 2 / 3;

    int col1 = w / 4;
    int col2 = w / 2;
    int col3 = w * 3 / 4;

    locateWindow(clangWin,   0,    0,    w,    topH);
    locateWindow(codeWin,    0,    midY, col1, botY);
    locateWindow(sourceWin,  col1, midY, col2, botY);
    locateWindow(stackWin,   col2, midY, col3, h);
    locateWindow(displayWin, col3, midY, w,    h);
    locateWindow(ioWin,      0,    botY, col2, h);

    deskTop->insert(ioWin);
    deskTop->insert(displayWin);
    deskTop->insert(stackWin);
    deskTop->insert(sourceWin);
    deskTop->insert(codeWin);
    deskTop->insert(clangWin);
}

void VisiC4App::switchMode(AppMode mode)
{
    if (mode == currentMode)
        return;
    removeAllWindows();
    currentMode = mode;
    if (mode == mCompile)
        layoutCompileMode();
    else {
        layoutRuntimeMode();
        sourceWin->viewer->setText(inputWin->viewer->lines);
    }
    deskTop->redraw();
}

void VisiC4App::tileWindows()
{
    removeAllWindows();
    if (currentMode == mCompile)
        layoutCompileMode();
    else
        layoutRuntimeMode();
    deskTop->redraw();
}

TMenuBar *VisiC4App::initMenuBar(TRect r) noexcept
{
    r.b.y = r.a.y + 1;
    return new TMenuBar(r,
        *new TSubMenu("~F~ile", kbAltF, hcNoContext) +
            *new TMenuItem("~L~oad Source...", cmLoadSource, kbCtrlO, hcNoContext, "Ctrl-O") +
            newLine() +
            *new TMenuItem("S~u~spend", cmDosShell, kbNoKey, hcNoContext) +
            *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X") +

        *new TSubMenu("~C~ompile", kbAltC, hcNoContext) +
            *new TMenuItem("~C~ompile", cmCompile, kbCtrlP, hcNoContext, "Ctrl-P") +
            *new TMenuItem("~S~tep Over", cmStepOver, kbCtrlS, hcNoContext, "Ctrl-S") +
            *new TMenuItem("C~y~cle", cmCycle, kbCtrlY, hcNoContext, "Ctrl-Y") +
            *new TMenuItem("Co~n~tinue", cmContinueTrace, kbCtrlN, hcNoContext, "Ctrl-N") +
            *new TMenuItem("S~t~op", cmStop, kbCtrlB, hcNoContext, "Ctrl-B") +
            newLine() +
            *new TMenuItem("S~p~eed...", cmCompileSpeed, kbNoKey, hcNoContext) +
            *new TMenuItem("C~l~ear Breakpoints", cmClearBreaks, kbCtrlK, hcNoContext, "Ctrl-K") +

        *new TSubMenu("~R~un", kbAltR, hcNoContext) +
            *new TMenuItem("~R~un", cmRun, kbCtrlR, hcNoContext, "Ctrl-R") +
            *new TMenuItem("~S~tep Over", cmStepOver, kbCtrlS, hcNoContext, "Ctrl-S") +
            *new TMenuItem("C~y~cle", cmCycle, kbCtrlY, hcNoContext, "Ctrl-Y") +
            *new TMenuItem("Co~n~tinue", cmContinueTrace, kbCtrlN, hcNoContext, "Ctrl-N") +
            *new TMenuItem("S~t~op", cmStop, kbCtrlB, hcNoContext, "Ctrl-B") +
            newLine() +
            *new TMenuItem("S~p~eed...", cmCompileSpeed, kbNoKey, hcNoContext) +
            *new TMenuItem("C~l~ear Breakpoints", cmClearBreaks, kbCtrlK, hcNoContext, "Ctrl-K") +
            newLine() +
            *new TMenuItem("Re~s~tart", cmRestart, kbCtrlT, hcNoContext, "Ctrl-T") +

        *new TSubMenu("~W~indow", kbAltW, hcNoContext) +
            *new TMenuItem("~S~witch Mode", cmSwitchMode, kbCtrlW, hcNoContext, "Ctrl-W") +
            newLine() +
            *new TMenuItem("~D~efault Layout", cmTileWindows, kbNoKey, hcNoContext) +
            *new TMenuItem("C~a~scade", cmCascadeWindows, kbNoKey, hcNoContext) +
            newLine() +
            *new TMenuItem("~C~4", cmShowClang, kbNoKey, hcNoContext) +
            *new TMenuItem("~G~rammar", cmShowGrammar, kbNoKey, hcNoContext) +
            *new TMenuItem("~P~rocedure List", cmShowProcList, kbNoKey, hcNoContext) +
            *new TMenuItem("~S~ource", cmShowInput, kbNoKey, hcNoContext) +
            *new TMenuItem("C~o~de", cmShowCode, kbNoKey, hcNoContext) +
            *new TMenuItem("So~u~rce (RT)", cmShowSource, kbNoKey, hcNoContext) +
            *new TMenuItem("Stac~k~", cmShowStack, kbNoKey, hcNoContext) +
            *new TMenuItem("~D~isplay", cmShowDisplay, kbNoKey, hcNoContext) +
            *new TMenuItem("I/~O~", cmShowIO, kbNoKey, hcNoContext)
    );
}

TStatusLine *VisiC4App::initStatusLine(TRect r) noexcept
{
    r.a.y = r.b.y - 1;
    return new TStatusLine(r,
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit) +
            *new TStatusItem("~Alt-C~ Compile", kbAltC, cmMenu) +
            *new TStatusItem("~Alt-R~ Run", kbAltR, cmMenu) +
            *new TStatusItem("~Alt-W~ Window", kbAltW, cmMenu)
    );
}

void VisiC4App::handleEvent(TEvent &event)
{
    TApplication::handleEvent(event);

    if (event.what == evCommand)
    {
        switch (event.message.command)
        {
            case cmLoadSource:
                loadSourceFile();
                clearEvent(event);
                break;
            case cmCompile:
                doCompile();
                clearEvent(event);
                break;
            case cmCompileSpeed:
                doSpeedDialog();
                clearEvent(event);
                break;
            case cmClearBreaks:
                clearBreakpoints();
                clearEvent(event);
                break;
            case cmContinueTrace:
                continueTrace();
                clearEvent(event);
                break;
            case cmStop:
                doStop();
                clearEvent(event);
                break;
            case cmRun:
                doRun();
                clearEvent(event);
                break;
            case cmCycle:
                doCycle();
                clearEvent(event);
                break;
            case cmStepOver:
                doStepOver();
                clearEvent(event);
                break;
            case cmRestart:
                doRestart();
                clearEvent(event);
                break;
            case cmTileWindows:
                tileWindows();
                clearEvent(event);
                break;
            case cmCascadeWindows:
                deskTop->cascade(deskTop->getExtent());
                clearEvent(event);
                break;
            case cmShowClang:
                if (clangWin) clangWin->select();
                clearEvent(event);
                break;
            case cmShowGrammar:
                if (grammarWin) grammarWin->select();
                clearEvent(event);
                break;
            case cmShowProcList:
                if (procListWin) procListWin->select();
                clearEvent(event);
                break;
            case cmShowInput:
                if (inputWin) inputWin->select();
                clearEvent(event);
                break;
            case cmShowCode:
                if (codeWin) codeWin->select();
                clearEvent(event);
                break;
            case cmShowStack:
                if (stackWin)
                {
                    if (!stackWin->owner)
                        deskTop->insert(stackWin);
                    stackWin->select();
                }
                clearEvent(event);
                break;
            case cmShowDisplay:
                if (displayWin)
                {
                    if (!displayWin->owner)
                        deskTop->insert(displayWin);
                    displayWin->select();
                }
                clearEvent(event);
                break;
            case cmShowSource:
                if (sourceWin)
                {
                    if (!sourceWin->owner)
                        deskTop->insert(sourceWin);
                    sourceWin->select();
                }
                clearEvent(event);
                break;
            case cmShowIO:
                if (ioWin)
                {
                    if (!ioWin->owner)
                        deskTop->insert(ioWin);
                    ioWin->select();
                }
                clearEvent(event);
                break;
            case cmSwitchMode:
                switchMode(currentMode == mCompile ? mRuntime : mCompile);
                clearEvent(event);
                break;
        }
    }
}

void VisiC4App::idle()
{
    TApplication::idle();
    if ((trace.active || runTraceActive) && !tracePaused) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStepTime).count();
        if (elapsed >= compileDelayMs) {
            lastStepTime = now;
            if (trace.active)
                compileStep();
            else if (runTraceActive)
                runStep();
        }
    }
}

void VisiC4App::compileStep()
{
    // Phase 1: Need a new token? Call next().
    if (trace.tokenEmitted) {
        if (!trace.next()) {
            // Source exhausted — reveal remaining code, clear highlights
            while (codeRevealPos < (int)compiledCode.size())
                codeWin->viewer->addLine(compiledCode[codeRevealPos++]);
            clangWin->viewer->highlightLine = -1;
            clangWin->viewer->drawView();
            grammarWin->viewer->highlightLine = -1;
            grammarWin->viewer->drawView();
            inputWin->viewer->highlightLine = -1;
            inputWin->viewer->drawView();
            messageBox("Compiled Correctly", mfInformation | mfOKButton);
            return;
        }
        // Source line highlight (stays for entire token)
        int srcLine = trace.line - 1;
        inputWin->viewer->highlightLine = srcLine;
        if (srcLine >= 0) {
            int viewH = inputWin->viewer->size.y;
            int sy = srcLine - viewH / 2;
            if (sy < 0) sy = 0;
            inputWin->viewer->scrollTo(inputWin->viewer->scrollX, sy);
        }
        inputWin->viewer->drawView();

        // Grammar highlight (stays for entire token)
        grammarWin->viewer->highlightLine = trace.grammarLine;
        grammarWin->viewer->drawView();
    }

    // Phase 2: Step through C4 source lines one at a time
    int c4line = trace.currentC4Line();
    if (c4line >= 0) {
        clangWin->viewer->highlightLine = c4line;
        int viewH = clangWin->viewer->size.y;
        int sy = c4line - viewH / 2;
        if (sy < 0) sy = 0;
        clangWin->viewer->scrollTo(clangWin->viewer->scrollX, sy);
        clangWin->viewer->drawView();

        // Check breakpoint on C4 line
        auto *nv = static_cast<NumberedTextViewer *>(clangWin->viewer);
        if (nv->breakpoints.count(c4line))
            tracePaused = true;

        if (!trace.advanceC4Line()) {
            // Last C4 line for this token — emit token on next call
            trace.tokenEmitted = true;

            // Add token to Token window
            char buf[128];
            snprintf(buf, sizeof(buf), "%4d  %s", trace.line, trace.tokenName().c_str());
            procListWin->viewer->addLine(buf);

            // Reveal next compiled code line
            if (codeRevealPos < (int)compiledCode.size()) {
                codeWin->viewer->addLine(compiledCode[codeRevealPos++]);
            }
        }
    }
}

void VisiC4App::loadSourceFile()
{
    TFileDialog *d = new TFileDialog("*.*", "Load Source File",
                                     "~N~ame", fdOpenButton, 100);
    if (deskTop->execView(d) != cmCancel)
    {
        char path[MAXPATH];
        d->getFileName(path);
        inputFilePath = path;

        // Read file into input window
        std::ifstream file(path);
        if (file.is_open())
        {
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line))
                lines.push_back(line);
            file.close();

            inputWin->viewer->setText(lines);

            // Update input window title
            std::string title = "Input : ";
            // Extract just the filename
            std::string fullPath = path;
            auto pos = fullPath.find_last_of("/\\");
            if (pos != std::string::npos)
                title += fullPath.substr(pos + 1);
            else
                title += fullPath;
            // Can't easily change TWindow title, so we leave it as is
        }
        else
        {
            messageBox("Could not open file.", mfError | mfOKButton);
        }
    }
    destroy(d);
}

void VisiC4App::doCompile()
{
    if (inputWin->viewer->lines.empty())
    {
        messageBox("No source file loaded. Use File > Load Source first.",
                   mfInformation | mfOKButton);
        return;
    }

    // Switch to compile mode
    switchMode(mCompile);

    // Clear output windows
    procListWin->viewer->clear();
    procListWin->items.clear();

    // Compile source to generate correct bytecode
    std::string src;
    for (auto &l : inputWin->viewer->lines)
        src += l + "\n";
    compiledCode = compiler.compile(src);
    codeRevealPos = 0;
    codeWin->viewer->clear(); // code revealed gradually during trace

    // Start compile trace (token stepping only)
    trace.init(inputWin->viewer->lines);
    lastStepTime = std::chrono::steady_clock::now();
}

class TSpeedDialog : public TDialog
{
public:
    TScrollBar *bar;
    TStaticText *valText;

    TSpeedDialog(int curVal) :
        TWindowInit(&TDialog::initFrame),
        TDialog(TRect(15, 5, 55, 14), "Compile Speed")
    {
        insert(new TStaticText(TRect(3, 2, 10, 3), "Fast"));
        insert(new TStaticText(TRect(32, 2, 37, 3), "Slow"));

        bar = new TScrollBar(TRect(3, 3, 37, 4));
        bar->setParams(curVal, 0, 500, 10, 50);
        insert(bar);

        valText = new TStaticText(TRect(12, 4, 28, 5), "");
        insert(valText);
        updateLabel();

        insert(new TButton(TRect(7, 6, 17, 8), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(22, 6, 34, 8), "Cancel", cmCancel, bfNormal));
    }

    void updateLabel()
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Delay: %d ms", bar->value);
        // Replace the static text content by removing and re-inserting
        remove(valText);
        destroy(valText);
        valText = new TStaticText(TRect(12, 4, 28, 5), buf);
        insert(valText);
    }

    void handleEvent(TEvent &event) override
    {
        TDialog::handleEvent(event);
        if (event.what == evBroadcast &&
            event.message.command == cmScrollBarChanged &&
            event.message.infoPtr == bar)
        {
            updateLabel();
            clearEvent(event);
        }
    }
};

void VisiC4App::doSpeedDialog()
{
    auto *dlg = new TSpeedDialog(compileDelayMs);

    if (deskTop->execView(dlg) == cmOK)
        compileDelayMs = dlg->bar->value;

    destroy(dlg);
}


void VisiC4App::clearBreakpoints()
{
    auto *nv = static_cast<NumberedTextViewer *>(clangWin->viewer);
    nv->breakpoints.clear();
    nv->drawView();
}

void VisiC4App::continueTrace()
{
    if ((trace.active || runTraceActive) && tracePaused) {
        tracePaused = false;
        lastStepTime = std::chrono::steady_clock::now();
    }
}

void VisiC4App::doRun()
{
    if (codeWin->viewer->lines.empty())
    {
        messageBox("No code to run. Compile first.", mfInformation | mfOKButton);
        return;
    }

    // Switch to runtime mode
    switchMode(mRuntime);

    // Copy source into sourceWin
    sourceWin->viewer->setText(inputWin->viewer->lines);

    // Initialize VM for step-by-step execution
    ioWin->viewer->clear();
    if (!compiler.initVM()) {
        messageBox("Cannot find main() entry point.", mfError | mfOKButton);
        return;
    }

    // Start run trace
    runTraceActive = true;
    tracePaused = false;
    lastStepTime = std::chrono::steady_clock::now();

    // Highlight VM loop entry in C4 window
    clangWin->viewer->highlightLine = 480; // while (1)
    clangWin->viewer->scrollTo(0, 470);
    clangWin->viewer->drawView();

    // Highlight first code line
    int dl = compiler.currentDisplayLine();
    if (dl >= 0) {
        codeWin->viewer->highlightLine = dl;
        codeWin->viewer->scrollTo(codeWin->viewer->scrollX, dl);
    }
    codeWin->viewer->drawView();
}

void VisiC4App::runStep()
{
    // Execute one VM instruction
    std::string out;
    int op = compiler.stepVM(out);

    // Show output first (before checking if VM finished)
    if (!out.empty())
        ioWin->viewer->addLine(out);

    if (op < 0 || !compiler.vmRunning) {
        // VM finished
        runTraceActive = false;
        tracePaused = false;
        clangWin->viewer->highlightLine = -1;
        clangWin->viewer->drawView();
        codeWin->viewer->highlightLine = -1;
        codeWin->viewer->drawView();
        messageBox("Run Finished", mfInformation | mfOKButton);
        return;
    }

    // Highlight current code line in Code window
    int dl = compiler.currentDisplayLine();
    if (dl >= 0) {
        codeWin->viewer->highlightLine = dl;
        int viewH = codeWin->viewer->size.y;
        int sy = dl - viewH / 2;
        if (sy < 0) sy = 0;
        codeWin->viewer->scrollTo(codeWin->viewer->scrollX, sy);
    }
    codeWin->viewer->drawView();

    // Highlight VM instruction line in C4 source
    if (op >= 0 && op < OP_COUNT) {
        int vmLine = vmLineForOp[op];
        clangWin->viewer->highlightLine = vmLine;
        int csy = vmLine - clangWin->viewer->size.y / 2;
        if (csy < 0) csy = 0;
        clangWin->viewer->scrollTo(clangWin->viewer->scrollX, csy);
        clangWin->viewer->drawView();

        // Check breakpoint on VM line
        auto *nv = static_cast<NumberedTextViewer *>(clangWin->viewer);
        if (nv->breakpoints.count(vmLine))
            tracePaused = true;
    }

    // Update Stack window
    {
        auto si = compiler.getStackInfo();
        stackWin->viewer->rows.clear();
        for (auto &e : si.entries) {
            TableViewer::Row r;
            r.index = e.offset;
            r.value = (int)e.value;
            stackWin->viewer->rows.push_back(r);
        }
        // Mark SP (top) and BP
        stackWin->viewer->marker = si.bpIdx;
        stackWin->viewer->markerLabel = "<BP";
        stackWin->viewer->syncScrollBar();
        stackWin->viewer->drawView();
    }

    // Update Display window with current frame variables
    {
        auto vars = compiler.getFrameVars();
        displayWin->viewer->rows.clear();
        auto *fi = compiler.currentFunction();
        if (fi) {
            // Function name header
            TableViewer::Row hdr;
            hdr.index = 0; hdr.value = 0;
            hdr.label = fi->name + "()";
            displayWin->viewer->rows.push_back(hdr);
        }
        for (auto &v : vars) {
            TableViewer::Row r;
            r.index = 0;
            r.value = (int)v.value;
            r.label = (v.isParam ? "P " : "L ") + v.name;
            displayWin->viewer->rows.push_back(r);
        }
        displayWin->viewer->syncScrollBar();
        displayWin->viewer->drawView();
    }
}

void VisiC4App::doCycle()
{
    if (runTraceActive) {
        // Run trace: execute until call depth returns to current level
        int targetDepth = compiler.vmCallDepth;
        tracePaused = false;
        do {
            runStep();
            if (!runTraceActive) break; // VM finished
            if (tracePaused) break;     // hit breakpoint
        } while (compiler.vmCallDepth > targetDepth);
        tracePaused = true;
    } else if (trace.active) {
        // Compile trace: step through all remaining C4 lines for current token
        tracePaused = false;
        while (trace.currentC4Line() >= 0) {
            compileStep();
            if (!trace.active) break;
            if (tracePaused) break; // hit breakpoint
        }
        // Also do one more step to fetch the next token
        if (trace.active && !tracePaused)
            compileStep();
        tracePaused = true;
    }
}

void VisiC4App::doStepOver()
{
    // Single-step: pause first, then step one instruction
    if (runTraceActive) {
        tracePaused = true;
        runStep();
    } else if (trace.active) {
        tracePaused = true;
        compileStep();
    }
}

void VisiC4App::doStop()
{
    if (trace.active || runTraceActive)
        tracePaused = true;
}

void VisiC4App::doRestart()
{
    // Reset execution state
    runTraceActive = false;
    trace.active = false;
    tracePaused = false;

    stackWin->viewer->rows.clear();
    ioWin->viewer->clear();
    sourceWin->viewer->clear();

    clangWin->viewer->highlightLine = -1;
    clangWin->viewer->drawView();
    codeWin->viewer->highlightLine = -1;
    codeWin->viewer->drawView();

    // Switch back to compile mode
    switchMode(mCompile);
}

/////////////////////////////////////////////////////////////////////////
// main

int main()
{
    VisiC4App app;
    app.run();
    app.shutDown();
    return 0;
}
