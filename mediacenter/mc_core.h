/*
    mc_core.h -- portable core of Classic Media Center 0.1 (now 0.4)

    Strict C89. No Mac headers, no 64-bit types, no malloc: every
    structure here is a fixed-size block the caller owns (a static
    global on the Mac is fine; the largest, McMeta, is about 4 KB).
    Byte order never matters: multi-byte values are assembled by hand.

    Pieces:
      a. mc_search_url()      -- build the archive.org search URL
      b. McJson               -- a push (incremental) JSON scanner:
                                 feed it bytes as they arrive from the
                                 network, any chunk size, even 1 byte
         McSearch             -- search results -> {identifier,title}
      c. McMeta               -- /metadata/<id> -> ranked MP4 files,
                                 servers, dir
      d. mc_attempt_urls()    -- the list of URLs to try for a file
      e. mc_avc_profile()     -- profile/level/size from a 'moov'
         mc_avc_verdict()     -- may our decoder play it?
         (0.4) mc_set_decoder_level() -- what the installed decoder
                                 can do (Baseline only, or Main/High)
*/
#ifndef MC_CORE_H
#define MC_CORE_H

/* ---------------- sizes (all fixed) ---------------- */
#define MC_URLMAX      1024   /* same as kMaxURL in H264Stream.c     */
#define MC_JSON_DEPTH  12     /* nesting deeper than this = error    */
#define MC_JSON_KEYMAX 32     /* keys are truncated to 31 chars      */
#define MC_JSON_STRMAX 256    /* strings are truncated to 255 chars  */
#define MC_MAXHITS     25     /* rows=25 in the search URL           */
#define MC_IDMAX       104    /* archive identifiers are <= 100      */
#define MC_TITLEMAX    96
#define MC_MAXCAND     8
#define MC_NAMEMAX     200
#define MC_FMTMAX      24
#define MC_MAXSRV      6
#define MC_SRVMAX      64
#define MC_DIRMAX      160
#define MC_MAXATTEMPT  (1 + MC_MAXSRV)
#define MC_MAXFILES    64     /* (0.3) the item's video file list    */

/* Largest picture we will hand to QuickTime. h264bsd itself has no
   hard cap (an SPS whose size does not fit its level only makes it
   size the DPB from num_ref_frames), but the codec's blitter is
   written around a 1280-pixel row (H264Codec.c), and a G3/G4 cannot
   decode more than that in real time anyway. */
#define MC_MAX_WIDTH   1280
#define MC_MAX_HEIGHT  1024

/* ---------------- a. search URL ---------------- */

/* Build the search URL for 'terms' (typed by the user, ASCII/MacRoman)
   and page number (1-based) into out[outMax]. Characters that would
   break the Lucene query, ( ) : [ ] { } ^ ~ " \ and every non-ASCII
   byte, become spaces; runs of spaces collapse. Returns 0 ok, -1 if
   the terms are empty after cleaning, -2 if out is too small. */
int mc_search_url(const char *terms, int page, char *out, long outMax);

/* (0.3) The query is built from the user's words like this:
     - every word is REQUIRED (joined with AND);
     - "a quoted phrase" stays one phrase;
     - plain words are lower-cased (so and/or/not are words, never
       Lucene operators); a word holding anything but letters and
       digits (c++, x-men, don't) is sent as a one-word phrase "..."
       -- archive.org's backend answers an error to backslash
       escapes, but takes any character inside quotes;
     - '"' and '\' and every non-ASCII byte separate words; a word
       with no letter or digit at all is dropped.
   scope MC_SCOPE_TITLE: title:(...) AND mediatype:movies
   scope MC_SCOPE_ANY:   (...) AND mediatype:movies   (every field)
   Both sorted by downloads: archive.org's advancedsearch has NO
   relevance order (without sort[] it returns index order, and
   sort[]=score is refused), so "title first, then everything" is our
   relevance. mc_search_url() = scope MC_SCOPE_TITLE.
   Returns 0 ok, -1 nothing to search for, -2 out too small. */
#define MC_SCOPE_TITLE 0
#define MC_SCOPE_ANY   1
int mc_search_url2(const char *terms, int scope, int page, char *out,
                   long outMax);
/* Only the q= part, unescaped (for the log and the tests). Same codes. */
int mc_search_query(const char *terms, int scope, char *out, long outMax);
/* (b19) bases: defaults "https://archive.org" / "http://archive.org" */
extern const char *mc_api_base;
extern const char *mc_dl_base;
/* <api_base>/metadata/<escaped id>. 0 ok, -1 too long / bad. */
int mc_meta_url(const char *id, char *out, long outMax);

/* Percent-encode s for a URL query (space -> '+'). 0 ok, -1 too long. */
int mc_query_escape(const char *s, char *out, long outMax);

/* Percent-encode s for a URL path ('/' kept, space -> %20).
   0 ok, -1 too long. */
int mc_path_escape(const char *s, char *out, long outMax);

/* ---------------- b. push JSON scanner ---------------- */

/* event kinds passed to the callback */
#define MC_J_STR       1     /* val/len = the (decoded) string      */
#define MC_J_NUM       2     /* val/len = the number's text         */
#define MC_J_LIT       3     /* val = "true" / "false" / "null"     */
#define MC_J_OBJ_BEGIN 4
#define MC_J_OBJ_END   5
#define MC_J_ARR_BEGIN 6
#define MC_J_ARR_END   7

struct McJson;
typedef void (*McJsonEvent)(void *user, const struct McJson *j, int kind,
                            const char *val, long len);

typedef struct McJson
{
    /* --- public, read-only inside the callback --- */
    int  depth;                              /* open containers */
    char kind[MC_JSON_DEPTH];                /* 'o' or 'a' per level */
    char key[MC_JSON_DEPTH][MC_JSON_KEYMAX]; /* current key per 'o' */
    long index[MC_JSON_DEPTH];               /* element # per 'a' */
    int  truncated;                          /* last string was cut */
    /* --- private --- */
    int  state, after, isKey, hexN, utfNeed;
    unsigned long hexV, utfCode, hiSurrogate;
    long slen;
    char str[MC_JSON_STRMAX];
    long bytes;
    int  err;
    McJsonEvent cb;
    void *user;
} McJson;

/* Scalar events: depth = containers enclosing the value; if
   kind[depth-1]=='o' the value's key is key[depth-1], if 'a' its
   position is index[depth-1]. BEGIN events come after the push (so
   depth counts the new container), END events before the pop. */

void mc_json_init(McJson *j, McJsonEvent cb, void *user);
/* 0 ok so far, -1 syntax error (sticky: later calls also return -1) */
int  mc_json_feed(McJson *j, const char *buf, long n);
/* 0 if exactly one complete top-level value was seen, -1 otherwise */
int  mc_json_finish(McJson *j);

/* Non-ASCII text (\uXXXX escapes and raw UTF-8) is folded to plain
   ASCII: accented Latin-1 letters lose their accent, a few
   punctuation marks become their ASCII look-alikes, everything else
   becomes '?'. Plain ASCII is MacRoman-safe. */

/* ---------------- search results ---------------- */

typedef struct
{
    char identifier[MC_IDMAX];
    char title[MC_TITLEMAX];
} McHit;

typedef struct
{
    McHit hits[MC_MAXHITS];
    int   count;
    long  numFound;        /* -1 if absent */
    int   cur;             /* private */
    int   titleSeen;       /* private */
    McHit tmp;             /* private */
    McJson j;
} McSearch;

void mc_search_begin(McSearch *s);
int  mc_search_feed(McSearch *s, const char *buf, long n); /* 0 / -1 */
int  mc_search_end(McSearch *s);   /* hit count, or -1 bad/truncated */

/* ---------------- c. item metadata ---------------- */

/* format ranks, best first */
#define MC_RANK_512KB  1      /* "512Kb MPEG4": IA's Baseline 320x240 */
#define MC_RANK_MPEG4  2      /* "MPEG4": often the uploader's own    */
#define MC_RANK_H264   3      /* "h.264", "h.264 HD", ...             */
#define MC_RANK_H264IA 4      /* "h.264 IA": High profile, last       */

typedef struct
{
    char name[MC_NAMEMAX];
    char format[MC_FMTMAX];
    unsigned long size;      /* bytes, clamped to 0xFFFFFFFF */
    int  width, height;      /* 0 if not stated */
    int  rank;
} McCand;

/* (0.3) one row of an item's video file list */
#define MC_HINT_NEWER  0      /* "needs newer decoder?" (a guess)     */
#define MC_HINT_PLAYS  1      /* "plays now" (a guess): h.264, 512Kb  */
#define MC_STATE_HINT  0      /* not checked yet: show the hint       */
#define MC_STATE_PLAYS 1      /* moov checked: Baseline (or QT's own) */
#define MC_STATE_NEWER (-1)   /* moov checked: Main/High/CABAC/other  */

typedef struct
{
    char name[MC_NAMEMAX];
    char title[MC_TITLEMAX];  /* the file's own "title", may be ""  */
    char format[MC_FMTMAX];
    unsigned long size;       /* bytes, clamped to 0xFFFFFFFF; 0 = ? */
    int  width, height;
    int  hint;                /* MC_HINT_*                           */
    int  state;               /* MC_STATE_*                          */
} McFile;

typedef struct
{
    McCand cand[MC_MAXCAND];   /* sorted: rank, then file order */
    int    ncand;
    int    droppedCand;        /* candidates that did not fit / too long */
    char   server[MC_MAXSRV][MC_SRVMAX];
    int    nsrv;
    char   dir[MC_DIRMAX];
    char   title[MC_TITLEMAX];
    /* (0.3) every .mp4/.m4v/.mov file, natural name order (1x2 < 1x10) */
    McFile files[MC_MAXFILES];
    int    nfiles;
    int    droppedFiles;       /* did not fit / name too long */
    int    isDark;             /* item withdrawn */
    int    topKeys;            /* top-level keys seen (0 = "{}") */
    /* private */
    McCand tmp;
    char   tmpTitle[MC_TITLEMAX];
    int    tmpBad;
    int    titleSeen;
    McJson j;
} McMeta;

void mc_meta_begin(McMeta *m);
int  mc_meta_feed(McMeta *m, const char *buf, long n);   /* 0 / -1 */
/* 0 ok, -1 bad/truncated JSON, -2 empty "{}" (no such item),
   -3 item is dark. Zero candidates is NOT an error: check ncand. */
int  mc_meta_end(McMeta *m);

/* (0.3) helpers for the file list */
/* strcmp, but case-blind and digit runs compared as numbers */
int  mc_natcmp(const char *a, const char *b);
/* "plays now" hint for a format label: MC_HINT_PLAYS / MC_HINT_NEWER.
   (0.4) level 1 also guesses "plays now" for "h.264 IA". */
int  mc_format_hint(const char *format);
/* readable name: the file's title, else the name without folders and
   without its extension */
void mc_file_label(const McFile *f, char *out, long outMax);
/* "plays now?" / "needs newer decoder?" / "PLAYS NOW" / "needs newer
   decoder" -- the mark shown in the list. (0.4) With the Main/High
   decoder (level 1) the two "no" marks read "may not play?" (a guess)
   and "will not play" (moov checked): a newer decoder is no longer the
   reason. Tokens: hint:may-not-play, fact:will-not-play. */
const char *mc_file_mark(const McFile *f);
/* grep-able token: hint:plays-now, hint:needs-newer-decoder,
   fact:plays-now, fact:needs-newer-decoder */
const char *mc_file_token(const McFile *f);
/* size as "376.4" (MB, one decimal) */
void mc_file_mb(const McFile *f, char *out);
/* the list row: "1x01 - The Cage   [h.264, 376.4 MB]  plays now?" */
void mc_file_row(const McFile *f, char *out, long outMax);
/* a McCand for the play code (rank from the format) */
void mc_file_cand(const McFile *f, McCand *c);

/* ---------------- (0.3) the test hook file ---------------- */
/* "Media Center Query": line 1 = search words; later lines may hold
   keywords (case-blind): download, insecure, confirm, list, file N. */
typedef struct
{
    char query[256];
    int  download;     /* tick "Download first"                    */
    int  insecure;     /* tick "Skip certificate checks (unsafe)"  */
    int  confirm;      /* allow a big download without asking      */
    int  list;         /* open hit 1, log its files, play nothing  */
    int  file;         /* 1-based file of hit 1 to play; 0 = none  */
    /* (0.5s) streaming hook runs: "serve N" = keep serving N s, then
       quit (closing the movie first); "stop N" = press Stop N s after
       the hand-off. 0 = not given */
    long serve;
    long stop;
} McHook;
/* text = the whole file (CR, LF or CRLF lines). 1 = there is a query */
int  mc_hook_parse(const char *text, McHook *h);

/* ---------------- d. URL attempts ---------------- */

/* Fill urls[0..] (each MC_URLMAX) for file c of item id:
     http://archive.org/download/<id>/<file>
     http://<server><dir>/<file>    for each server, *.us.* first.
   Returns the number of URLs (>=1), or -1 if even the first would
   not fit. */
int mc_attempt_urls(const char *id, const McMeta *m, const McCand *c,
                    char urls[][MC_URLMAX], int maxUrls);

/* ---------------- e. what is in the moov ---------------- */

typedef struct
{
    char codec[5];        /* sample entry type: "avc1", "mp4v", ... */
    int  profile;         /* profile_idc, -1 if no avcC */
    int  compat;          /* constraint_set flags byte */
    int  level;           /* level_idc */
    int  width, height;   /* from the visual sample entry */
    /* (0.4) from the first SPS in avcC; defaults when sps == 0 */
    int  sps;             /* 1 = that SPS was read up to frame_mbs_only */
    int  chroma;          /* chroma_format_idc: 0 mono, 1 4:2:0, 2, 3   */
    int  depth_luma;      /* bit depth, 8 = normal                     */
    int  depth_chroma;
    int  frame_mbs_only;  /* 0 = interlaced coding (fields / MBAFF)    */
} McAvcInfo;

/* moov = the whole 'moov' box, header included (as FetchMoov gives).
   Finds the first video sample entry in any trak's stsd.
   Returns 0 found, -1 no video sample entry, -2 malformed. */
int mc_avc_profile(const unsigned char *moov, long len, McAvcInfo *info);

#define MC_PLAY_OK          0   /* Baseline H.264 within size limits  */
#define MC_PLAY_NATIVE      1   /* mp4v etc.: QuickTime 6 plays it    */
#define MC_PLAY_NOT_BASE   -1   /* Main/High/...: refuse              */
#define MC_PLAY_TOO_BIG    -2
#define MC_PLAY_NO_VIDEO   -3
#define MC_PLAY_UNKNOWN    -4   /* hev1, vp09, avc1 without avcC ...  */
#define MC_PLAY_UNTESTED    2   /* (0.4) Main/High INTERLACED: handed to
                                   QuickTime, but flagged "untested"   */

/* (0.4) What the installed decoder can play, found once at start-up
   by the app (MediaCenter.c DecoderLevel: H264 Decoder >= 0x00010011
   AND AVCDecLib present) and set here. Default 0, so every program
   that never calls the setter (H264 Stream, the 0.3 tests) behaves
   exactly as 0.3. It decides: the verdict below, the list hints and
   marks, and whether BuildReferenceMovie runs the B-frame rewrite. */
#define MC_DECODER_BASELINE  0  /* h264bsd decoder: Baseline only      */
#define MC_DECODER_MAINHIGH  1  /* decoder 17 + AVCDecLib: Main/High   */
void mc_set_decoder_level(int level);
int  mc_decoder_level(void);

/* Verdict plus a one-line plain-English reason in msg[msgMax], for the
   decoder level set above. */
int mc_avc_verdict(const McAvcInfo *info, int found, char *msg, long msgMax);
/* (0.4) the same for an explicit level (tests) */
int mc_avc_verdict_at(const McAvcInfo *info, int found, int level,
                      char *msg, long msgMax);

#endif
