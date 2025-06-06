/*
   Copyright (c) 2000, 2012, Oracle and/or its affiliates.
   Copyright (c) 2017, 2022, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

/* This file is included by all internal myisam files */

#include <my_global.h>
#include <myisam.h>                     /* Structs & some defines */
#include <myisampack.h>                 /* packing of keys */
#include <my_tree.h>
#include <my_pthread.h>
#include <thr_lock.h>
#include <mysql/psi/mysql_file.h>

C_MODE_START

typedef struct st_mi_status_info
{
  ha_rows records;                      /* Rows in table */
  ha_rows del;                          /* Removed rows */
  my_off_t empty;                       /* lost space in datafile */
  my_off_t key_empty;                   /* lost space in indexfile */
  my_off_t key_file_length;
  my_off_t data_file_length;
  ha_checksum checksum;
  my_bool uncacheable;                  /* Active concurrent insert */
} MI_STATUS_INFO;

typedef struct st_mi_state_info
{
  struct
  {                                     /* Fileheader */
    uchar file_version[4];
    uchar options[2];
    uchar header_length[2];
    uchar state_info_length[2];
    uchar base_info_length[2];
    uchar base_pos[2];
    uchar key_parts[2];                 /* Key parts */
    uchar unique_key_parts[2];          /* Key parts + unique parts */
    uchar keys;                         /* number of keys in file */
    uchar uniques;                      /* number of UNIQUE definitions */
    uchar language;                     /* Language for indexes */
    uchar max_block_size_index;         /* max keyblock size */
    uchar fulltext_keys;
    uchar not_used;                     /* To align to 8 */
  } header;

  MI_STATUS_INFO state;
  ha_rows split;                        /* number of split blocks */
  my_off_t dellink;                     /* Link to next removed block */
  ulonglong auto_increment;
  ulong process;                        /* process that updated table last */
  ulong unique;                         /* Unique number for this process */
  ulong update_count;                   /* Updated for each write lock */
  ulong status;
  ulong *rec_per_key_part;
  ha_checksum checksum;                 /* Table checksum */
  my_off_t *key_root;                   /* Start of key trees */
  my_off_t *key_del;                    /* delete links for trees */
  my_off_t rec_per_key_rows;            /* Rows when calculating rec_per_key */

  ulong sec_index_changed;              /* Updated when new sec_index */
  ulong sec_index_used;                 /* which extra index are in use */
  ulonglong key_map;                    /* Which keys are in use */
  ulong version;                        /* timestamp of create */
  time_t create_time;                   /* Time when created database */
  time_t recover_time;                  /* Time for last recover */
  time_t check_time;                    /* Time for last check */
  uint sortkey;                         /* sorted by this key (not used) */
  uint open_count;
  uint8 changed;                        /* Changed since myisamchk */

  uint8 dupp_key;                       /* Lastly processed index with    */
                                        /* violated uniqueness constraint */

  /* the following isn't saved on disk */
  uint state_diff_length;               /* Should be 0 */
  uint state_length;                    /* Length of state header in file */
  ulong *key_info;
} MI_STATE_INFO;

#define MI_STATE_INFO_SIZE      (24+14*8+7*4+2*2+8)
#define MI_STATE_KEY_SIZE       8U
#define MI_STATE_KEYBLOCK_SIZE  8U
#define MI_STATE_KEYSEG_SIZE    4U
#define MI_STATE_EXTRA_SIZE ((MI_MAX_KEY+MI_MAX_KEY_BLOCK_SIZE)*MI_STATE_KEY_SIZE + MI_MAX_KEY*HA_MAX_KEY_SEG*MI_STATE_KEYSEG_SIZE)
#define MI_KEYDEF_SIZE          (2+ 5*2)
#define MI_UNIQUEDEF_SIZE       (2+1+1)
#define HA_KEYSEG_SIZE          (6+ 2*2 + 4*2)
#define MI_COLUMNDEF_SIZE       (2*3+1)
#define MI_BASE_INFO_SIZE       (5*8 + 8*4 + 4 + 4*2 + 16)
#define MI_INDEX_BLOCK_MARGIN   16U      /* Safety margin for .MYI tables */

typedef struct st_mi_base_info
{
  my_off_t keystart;                    /* Start of keys */
  my_off_t max_data_file_length;
  my_off_t max_key_file_length;
  my_off_t margin_key_file_length;
  ha_rows records, reloc;               /* Create information */
  ulong mean_row_length;                /* Create information */
  ulong reclength;                      /* length of unpacked record */
  ulong pack_reclength;                 /* Length of full packed rec. */
  ulong min_pack_length;
  ulong max_pack_length;                /* Max possibly length of packed rec.*/
  ulong min_block_length;
  ulong fields,                         /* fields in table */
    pack_fields;                        /* packed fields in table */
  uint rec_reflength;                   /* = 2-8 */
  uint key_reflength;                   /* = 2-8 */
  uint keys;                            /* same as in state.header */
  uint auto_key;                        /* Which key-1 is a auto key */
  uint blobs;                           /* Number of blobs */
  uint pack_bits;                       /* Length of packed bits */
  uint max_key_block_length;            /* Max block length */
  uint max_key_length;                  /* Max key length */
  /* Extra allocation when using dynamic record format */
  uint extra_alloc_bytes;
  uint extra_alloc_procent;
  /* The following are from the header */
  uint key_parts, all_key_parts, base_key_parts;
} MI_BASE_INFO;


        /* Structs used intern in database */

typedef struct st_mi_blob               /* Info of record */
{
  ulong offset;                         /* Offset to blob in record */
  uint pack_length;                     /* Type of packed length */
  ulong length;                         /* Calc:ed for each record */
} MI_BLOB;


typedef struct st_mi_isam_pack
{
  ulong header_length;
  uint ref_length;
  uchar version;
} MI_PACK;

#define MAX_NONMAPPED_INSERTS 1000

typedef struct st_mi_isam_share
{                                       /* Shared between opens */
  MI_STATE_INFO state;
  MI_BASE_INFO base;
  MI_KEYDEF  ft2_keyinfo;		/* Second-level ft-key definition */
  MI_KEYDEF  *keyinfo;			/* Key definitions */
  MI_UNIQUEDEF *uniqueinfo;		/* unique definitions */
  HA_KEYSEG *keyparts;			/* key part info */
  MI_COLUMNDEF *rec;			/* Pointer to field information */
  MI_PACK    pack;			/* Data about packed records */
  MI_BLOB    *blobs;			/* Pointer to blobs */
  LIST *in_use;                         /* List of threads using this table */
  char  *unique_file_name;		/* realpath() of index file */
  char  *data_file_name,		/* Resolved path names from symlinks */
        *index_file_name;
  uchar *file_map;			/* mem-map of file if possible */
  KEY_CACHE *key_cache;			/* ref to the current key cache */
  /* To mark the key cache partitions containing dirty pages for this file */ 
  ulonglong dirty_part_map;   
  MI_DECODE_TREE *decode_trees;
  uint16 *decode_tables;
  /* Function to use for a row checksum. */
  int(*read_record) (struct st_myisam_info *, my_off_t, uchar*);
  int(*write_record) (struct st_myisam_info *, const uchar*);
  int(*update_record) (struct st_myisam_info *, my_off_t, const uchar*);
  int(*delete_record) (struct st_myisam_info *);
  int(*read_rnd) (struct st_myisam_info *, uchar*, my_off_t, my_bool);
  int(*compare_record) (struct st_myisam_info *, const uchar*);
  ha_checksum(*calc_checksum) (struct st_myisam_info *, const uchar*);
  /* calculate checksum for a row during check table */
  ha_checksum(*calc_check_checksum)(struct st_myisam_info *, const uchar *);
  int(*compare_unique) (struct st_myisam_info *, MI_UNIQUEDEF *,
                        const uchar *record, my_off_t pos);
    size_t (*file_read) (MI_INFO *, uchar *, size_t, my_off_t, myf);
    size_t (*file_write) (MI_INFO *, const uchar *, size_t, my_off_t, myf);
  invalidator_by_filename invalidator;  /* query cache invalidator */
  /* query cache invalidator for changing state */
  invalidator_by_filename chst_invalidator;
  ulong this_process;                   /* processid */
  ulong last_process;                   /* For table-change-check */
  ulong last_version;                   /* Version on start */
  ulong options;                        /* Options used */
  ulong min_pack_length;                /* These are used by packed data */
  ulong max_pack_length;
  ulong state_diff_length;
  uint	rec_reflength;			/* rec_reflength in use now */
  ulong vreclength;                     /* full reclength, including vcols */
  uint  unique_name_length;
  uint32 ftkeys;                        /* Number of full-text keys + 1 */
  File	kfile;				/* Shared keyfile */
  File	data_file;			/* Shared data file */
  int	mode;				/* mode of file on open */
  uint	reopen;				/* How many times reopened */
  uint	w_locks,r_locks,tot_locks;	/* Number of read/write locks */
  uint	blocksize;			/* blocksize of keyfile */
  myf write_flag;
  enum data_file_type data_file_type;
  /* Below flag is needed to make log tables work with concurrent insert */
  my_bool is_log_table;
  /* This is 1 if they table checksum is of old type */
  my_bool has_null_fields;
  my_bool has_varchar_fields;

  my_bool changed,                      /* If changed since lock */
    global_changed,                     /* If changed since open */
    not_flushed, temporary, delay_key_write, concurrent_insert;
  my_bool deleting;                     /* we are going to delete this table */
  THR_LOCK lock;
  mysql_mutex_t intern_lock;            /* Locking for use with _locking */
  mysql_rwlock_t *key_root_lock;
  size_t mmaped_length;
  uint     nonmmaped_inserts;           /* counter of writing in non-mmaped
                                           area */
  mysql_rwlock_t mmap_lock;
} MYISAM_SHARE;


struct st_myisam_info
{
  MYISAM_SHARE *s;                      /* Shared between opens */
  MI_STATUS_INFO *state, save_state;
  MI_BLOB *blobs;                       /* Pointer to blobs */
  MI_BIT_BUFF bit_buff;
  /* accumulate indexfile changes between writes */
  TREE *bulk_insert;
  DYNAMIC_ARRAY *ft1_to_ft2;            /* used only in ft1->ft2 conversion */
  MEM_ROOT      ft_memroot;             /* used by the parser               */
  MYSQL_FTPARSER_PARAM *ftparser_param; /* share info between init/deinit   */
  void *external_ref;			/* For MariaDB TABLE */
  LIST in_use;                          /* Thread using this table          */
  char *filename;			/* parameter to open filename       */
  uchar *buff,				/* Temp area for key                */
	*lastkey,*lastkey2;		/* Last used search key             */
  uchar *first_mbr_key;			/* Searhed spatial key              */
  uchar	*rec_buff;			/* Tempbuff for recordpack          */
  uchar *int_keypos,			/* Save position for next/previous  */
        *int_maxpos;			/*  -""-  */
  uint  int_nod_flag;			/*  -""-  */
  uint32 int_keytree_version;		/*  -""-  */
  int (*read_record)(struct st_myisam_info*, my_off_t, uchar*);
  invalidator_by_filename invalidator;  /* query cache invalidator */
  ulong this_unique;                    /* unique filenumber or thread */
  ulong last_unique;                    /* last unique number */
  ulong this_loop;                      /* counter for this open */
  ulong last_loop;                      /* last used counter */
  my_off_t lastpos,                     /* Last record position */
    nextpos;                            /* Position to next record */
  my_off_t save_lastpos;
  my_off_t pos;                         /* Intern variable */
  my_off_t last_keypage;                /* Last key page read */
  my_off_t last_search_keypage;         /* Last keypage when searching */
  my_off_t dupp_key_pos;
  ha_checksum checksum;                 /* Temp storage for row checksum */
  /*
    QQ: the following two xxx_length fields should be removed,
     as they are not compatible with parallel repair
  */
  ulong packed_length, blob_length;     /* Length of found, packed record */
  int dfile;                            /* The datafile */
  uint open_flag;                       /* Parameters for open */
  uint opt_flag;                        /* Optim. for space/speed */
  uint once_flags;                      /* For MYISAMMRG */
  uint update;                          /* If file changed since open */
  int lastinx;                          /* Last used index */
  uint lastkey_length;                  /* Length of key in lastkey */
  uint last_rkey_length;                /* Last length in mi_rkey() */
  enum ha_rkey_function last_key_func;  /* CONTAIN, OVERLAP, etc */
  uint save_lastkey_length;
  uint pack_key_length;                 /* For MYISAMMRG */
  uint16 last_used_keyseg;              /* For MyISAMMRG */
  int errkey;                           /* Got last error on this key */
  int lock_type;                        /* How database was locked */
  int tmp_lock_type;                    /* When locked by readinfo */
  uint data_changed;                    /* Somebody has changed data */
  uint save_update;                     /* When using KEY_READ */
  int save_lastinx;
  LIST open_list;
  IO_CACHE rec_cache;                   /* When cacheing records */
  uint preload_buff_size;               /* When preloading indexes */
  myf lock_wait;                        /* is 0 or MY_SHORT_WAIT */
  my_bool was_locked;                   /* Was locked in panic */
  my_bool intern_lock_locked;           /* locked in mi_extra() */
  my_bool append_insert_at_end;         /* Set if concurrent insert */
  my_bool quick_mode;
  /* If info->buff can't be used for rnext */
  my_bool page_changed;
  /* If info->buff has to be reread for rnext */
  my_bool buff_used;
  my_bool create_unique_index_by_sort;
  my_bool has_cond_pushdown;
  index_cond_func_t index_cond_func;   /* Index condition function */
  void *index_cond_func_arg;           /* parameter for the func */
  rowid_filter_func_t rowid_filter_func;   /* rowid filter check function */
  void *rowid_filter_func_arg;             /* parameter for the func */
  THR_LOCK_DATA lock;
  uchar *rtree_recursion_state;         /* For RTREE */
  int rtree_recursion_depth;
};

#define USE_WHOLE_KEY   (HA_MAX_KEY_BUFF*2) /* Use whole key in _mi_search() */
#define F_EXTRA_LCK     -1
/* bits in opt_flag */
#define MEMMAP_USED     32U
#define REMEMBER_OLD_POS 64U

#define WRITEINFO_UPDATE_KEYFILE        1U
#define WRITEINFO_NO_UNLOCK             2U

/* once_flags */
#define USE_PACKED_KEYS         1U
#define RRND_PRESERVE_LASTINX   2U

/* bits in state.changed */
#define STATE_CHANGED           1U
#define STATE_CRASHED           2U
#define STATE_CRASHED_ON_REPAIR 4U
#define STATE_NOT_ANALYZED      8U
#define STATE_NOT_OPTIMIZED_KEYS 16U
#define STATE_NOT_SORTED_PAGES  32U

/* options to mi_read_cache */
#define READING_NEXT    1U
#define READING_HEADER  2U

#define mi_getint(x)    ((uint) mi_uint2korr(x) & 32767)
#define mi_putint(x,y,nod) { uint16 boh=(nod ? (uint16) 32768 : 0) + (uint16) (y);\
                          mi_int2store(x,boh); }
#define mi_test_if_nod(x) (x[0] & 128 ? info->s->base.key_reflength : 0)
#define mi_report_crashed(A, B) _mi_report_crashed((A), (B), __FILE__, __LINE__)
#define mi_mark_crashed(x) do{(x)->s->state.changed|= STATE_CRASHED; \
                              DBUG_PRINT("error", ("Marked table crashed")); \
                              mi_report_crashed((x), 0); \
                           }while(0)
#define mi_mark_crashed_on_repair(x) do{(x)->s->state.changed|= \
                                        STATE_CRASHED|STATE_CRASHED_ON_REPAIR; \
                                        (x)->update|= HA_STATE_CHANGED; \
                                        DBUG_PRINT("error", \
                                                   ("Marked table crashed")); \
                                     }while(0)
#define mi_is_crashed(x) ((x)->s->state.changed & STATE_CRASHED)
#define mi_is_crashed_on_repair(x) ((x)->s->state.changed & STATE_CRASHED_ON_REPAIR)
#define mi_print_error(SHARE, ERRNO)                     \
        mi_report_error((ERRNO), (SHARE)->index_file_name)

/* Functions to store length of space packed keys, VARCHAR or BLOB keys */

#define store_key_length(key,length) \
{ if ((length) < 255) \
  { *(key)=(length); } \
  else \
  { *(key)=255; mi_int2store((key)+1,(length)); } \
}

#define get_key_full_length(length,key) \
{ if ((uchar) *(key) != 255) \
    length= ((uint) (uchar) *((key)++))+1; \
  else \
  { length=mi_uint2korr((key)+1)+3; (key)+=3; } \
}

#define get_key_full_length_rdonly(length,key) \
{ if ((uchar) *(key) != 255) \
    length= ((uint) (uchar) *((key)))+1; \
  else \
  { length=mi_uint2korr((key)+1)+3; } \
}

#define get_pack_length(length) ((length) >= 255 ? 3 : 1)

#define MI_MIN_BLOCK_LENGTH     20      /* Because of delete-link */
#define MI_EXTEND_BLOCK_LENGTH  20      /* Don't use to small record-blocks */
#define MI_SPLIT_LENGTH ((MI_EXTEND_BLOCK_LENGTH+4)*2)
#define MI_MAX_DYN_BLOCK_HEADER 20      /* Max prefix of record-block */
#define MI_BLOCK_INFO_HEADER_LENGTH 20
#define MI_DYN_DELETE_BLOCK_HEADER 20   /* length of delete-block-header */
#define MI_DYN_MAX_BLOCK_LENGTH ((1UL << 24)-4UL)
#define MI_DYN_MAX_ROW_LENGTH   (MI_DYN_MAX_BLOCK_LENGTH - MI_SPLIT_LENGTH)
#define MI_DYN_ALIGN_SIZE       4U      /* Align blocks on this */
#define MI_MAX_DYN_HEADER_BYTE  13      /* max header byte for dynamic rows */
#define MI_MAX_BLOCK_LENGTH     (((1U << 24)-1) & (~(MI_DYN_ALIGN_SIZE-1)))
#define MI_REC_BUFF_OFFSET      ALIGN_SIZE(MI_DYN_DELETE_BLOCK_HEADER+sizeof(uint32))


#define PACK_TYPE_SELECTED      1U      /* Bits in field->pack_type */
#define PACK_TYPE_SPACE_FIELDS  2U
#define PACK_TYPE_ZERO_FILL     4U
#define MI_FOUND_WRONG_KEY 0x7FFFFFFF   /* Impossible value from ha_key_cmp */

#define MI_MAX_KEY_BLOCK_SIZE   (MI_MAX_KEY_BLOCK_LENGTH/MI_MIN_KEY_BLOCK_LENGTH)
#define MI_BLOCK_SIZE(key_length,data_pointer,key_pointer,block_size) (((((key_length)+(data_pointer)+(key_pointer))*4+(key_pointer)+2)/(block_size)+1)*(block_size))
#define MI_MAX_KEYPTR_SIZE      5       /* For calculating block lengths */
#define MI_MIN_KEYBLOCK_LENGTH  50      /* When to split delete blocks */

#define MI_MIN_SIZE_BULK_INSERT_TREE 16384U /* this is per key */
#define MI_MIN_ROWS_TO_USE_BULK_INSERT 100
#define MI_MIN_ROWS_TO_DISABLE_INDEXES 100
#define MI_MIN_ROWS_TO_USE_WRITE_CACHE 10

/* The UNIQUE check is done with a hashed long key */

#define MI_UNIQUE_HASH_TYPE     HA_KEYTYPE_ULONG_INT
#define mi_unique_store(A,B)    mi_int4store((A),(B))

extern mysql_mutex_t THR_LOCK_myisam;
#ifdef DONT_USE_RW_LOCKS
#define mysql_rwlock_wrlock(A) {}
#define mysql_rwlock_rdlock(A) {}
#define mysql_rwlock_unlock(A) {}
#endif

/* Some extern variables */

extern LIST *myisam_open_list;
extern uchar myisam_file_magic[], myisam_pack_file_magic[];
extern uint myisam_read_vec[], myisam_readnext_vec[];
extern uint myisam_quick_table_bits;
extern File myisam_log_file;
extern ulong myisam_pid;
extern my_bool (*mi_killed)(MI_INFO *);
extern void _mi_report_crashed(MI_INFO *file, const char *message,
                                  const char *sfile, uint sline);
/* This is used by _mi_calc_xxx_key_length och _mi_store_key */

typedef struct st_mi_s_param
{
  uint ref_length, key_length,
    n_ref_length,
    n_length, totlength, part_of_prev_key, prev_length, pack_marker;
  uchar *key, *prev_key, *next_key_pos;
  my_bool store_not_null;
} MI_KEY_PARAM;

/* Prototypes for intern functions */

extern int _mi_read_dynamic_record(MI_INFO *info, my_off_t filepos, uchar *buf);
extern int _mi_write_dynamic_record(MI_INFO *, const uchar *);
extern int _mi_update_dynamic_record(MI_INFO *, my_off_t, const uchar *);
extern int _mi_delete_dynamic_record(MI_INFO *info);
extern int _mi_cmp_dynamic_record(MI_INFO *info, const uchar *record);
extern int _mi_read_rnd_dynamic_record(MI_INFO *, uchar *, my_off_t, my_bool);
extern int _mi_write_blob_record(MI_INFO *, const uchar *);
extern int _mi_update_blob_record(MI_INFO *, my_off_t, const uchar *);
extern int _mi_read_static_record(MI_INFO *info, my_off_t filepos, uchar *buf);
extern int _mi_write_static_record(MI_INFO *, const uchar *);
extern int _mi_update_static_record(MI_INFO *, my_off_t, const uchar *);
extern int _mi_delete_static_record(MI_INFO *info);
extern int _mi_cmp_static_record(MI_INFO *info, const uchar *record);
extern int _mi_read_rnd_static_record(MI_INFO *, uchar *, my_off_t, my_bool);
extern int _mi_ck_write(MI_INFO *info, uint keynr, uchar *key, uint length);
extern int _mi_ck_real_write_btree(MI_INFO *info, MI_KEYDEF *keyinfo,
                                   uchar *key, uint key_length,
                                   my_off_t *root, uint comp_flag);
extern int _mi_enlarge_root(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *key,
                            my_off_t *root);
extern int _mi_insert(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *key,
                      uchar *anc_buff, uchar *key_pos, uchar *key_buff,
                      uchar *father_buff, uchar *father_keypos,
                      my_off_t father_page, my_bool insert_last);
extern int _mi_split_page(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *key,
                          uchar *buff, uchar *key_buff, my_bool insert_last);
extern uchar *_mi_find_half_pos(uint nod_flag, MI_KEYDEF *keyinfo,
                                uchar *page, uchar *key,
                                uint *return_key_length, uchar ** after_key);
extern int _mi_calc_static_key_length(MI_KEYDEF *keyinfo, uint nod_flag,
                                      uchar *key_pos, uchar *org_key,
                                      uchar *key_buff, uchar *key,
                                      MI_KEY_PARAM *s_temp);
extern int _mi_calc_var_key_length(MI_KEYDEF *keyinfo, uint nod_flag,
                                   uchar *key_pos, uchar *org_key,
                                   uchar *key_buff, uchar *key,
                                   MI_KEY_PARAM *s_temp);
extern int _mi_calc_var_pack_key_length(MI_KEYDEF *keyinfo, uint nod_flag,
                                        uchar *key_pos, uchar *org_key,
                                        uchar *prev_key, uchar *key,
                                        MI_KEY_PARAM *s_temp);
extern int _mi_calc_bin_pack_key_length(MI_KEYDEF *keyinfo, uint nod_flag,
                                        uchar *key_pos, uchar *org_key,
                                        uchar *prev_key, uchar *key,
                                        MI_KEY_PARAM *s_temp);
void _mi_store_static_key(MI_KEYDEF *keyinfo, uchar *key_pos,
                          MI_KEY_PARAM *s_temp);
void _mi_store_var_pack_key(MI_KEYDEF *keyinfo, uchar *key_pos,
                            MI_KEY_PARAM *s_temp);
void _mi_store_bin_pack_key(MI_KEYDEF *keyinfo, uchar *key_pos,
                            MI_KEY_PARAM *s_temp);

extern int _mi_ck_delete(MI_INFO *info, uint keynr, uchar *key,
                         uint key_length);
extern int _mi_readinfo(MI_INFO *info, int lock_flag, int check_keybuffer);
extern int _mi_writeinfo(MI_INFO *info, uint options);
extern int _mi_test_if_changed(MI_INFO *info);
extern int _mi_mark_file_changed(MI_INFO *info);
extern int _mi_decrement_open_count(MI_INFO *info);
void _mi_report_crashed_ignore(MI_INFO *file, const char *message,
                               const char *sfile, uint sline);
extern int _mi_check_index(MI_INFO *info, int inx);
extern int _mi_search(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *key,
                      uint key_len, uint nextflag, my_off_t pos);
extern int _mi_bin_search(struct st_myisam_info *info, MI_KEYDEF *keyinfo,
                          uchar *page, uchar *key, uint key_len,
                          uint comp_flag, uchar **ret_pos, uchar *buff,
                          my_bool *was_last_key);
extern int _mi_seq_search(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *page,
                          uchar *key, uint key_len, uint comp_flag,
                          uchar ** ret_pos, uchar *buff,
                          my_bool *was_last_key);
extern int _mi_prefix_search(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *page,
                             uchar *key, uint key_len, uint comp_flag,
                             uchar ** ret_pos, uchar *buff,
                             my_bool *was_last_key);
extern my_off_t _mi_kpos(uint nod_flag, uchar *after_key);
extern void _mi_kpointer(MI_INFO *info, uchar *buff, my_off_t pos);
extern my_off_t _mi_dpos(MI_INFO *info, uint nod_flag, uchar *after_key);
extern my_off_t _mi_rec_pos(MYISAM_SHARE *info, uchar *ptr);
extern void _mi_dpointer(MI_INFO *info, uchar *buff, my_off_t pos);
extern uint _mi_get_static_key(MI_KEYDEF *keyinfo, uint nod_flag,
                               uchar **page, uchar *key);
extern uint _mi_get_pack_key(MI_KEYDEF *keyinfo, uint nod_flag, uchar **page,
                             uchar *key);
extern uint _mi_get_binary_pack_key(MI_KEYDEF *keyinfo, uint nod_flag,
                                    uchar ** page_pos, uchar *key);
extern uchar *_mi_get_last_key(MI_INFO *info, MI_KEYDEF *keyinfo,
                               uchar *keypos, uchar *lastkey, uchar *endpos,
                               uint *return_key_length);
extern uchar *_mi_get_key(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *page,
                          uchar *key, uchar *keypos,
                          uint *return_key_length);
extern uint _mi_keylength(MI_KEYDEF *keyinfo, uchar *key);
extern uint _mi_keylength_part(MI_KEYDEF *keyinfo, uchar *key, HA_KEYSEG *end);
extern uchar *_mi_move_key(MI_KEYDEF *keyinfo, uchar *to, uchar *from);
extern int _mi_search_next(MI_INFO *info, MI_KEYDEF *keyinfo, uchar *key,
                           uint key_length, uint nextflag, my_off_t pos);
extern int _mi_search_first(MI_INFO *info, MI_KEYDEF *keyinfo, my_off_t pos);
extern int _mi_search_last(MI_INFO *info, MI_KEYDEF *keyinfo, my_off_t pos);
extern uchar *_mi_fetch_keypage(MI_INFO *info, MI_KEYDEF *keyinfo,
                                my_off_t page, int level, uchar *buff,
                                int return_buffer);
extern int _mi_write_keypage(MI_INFO *info, MI_KEYDEF *keyinfo, my_off_t page,
                             int level, uchar *buff);
extern int _mi_dispose(MI_INFO *info, MI_KEYDEF *keyinfo, my_off_t pos,
                       int level);
extern my_off_t _mi_new(MI_INFO *info, MI_KEYDEF *keyinfo, int level);
extern uint _mi_make_key(MI_INFO *info, uint keynr, uchar *key,
                         const uchar *record, my_off_t filepos);
extern uint _mi_pack_key(MI_INFO *info, uint keynr, uchar *key,
                         uchar *old, key_part_map keypart_map,
                         HA_KEYSEG ** last_used_keyseg);
extern int _mi_read_key_record(MI_INFO *info, my_off_t filepos, uchar *buf);
extern int _mi_read_cache(IO_CACHE *info, uchar *buff, my_off_t pos,
                          size_t length, int re_read_if_possibly);
extern ulonglong retrieve_auto_increment(MI_INFO *info, const uchar *record);

extern uchar *mi_alloc_rec_buff(MI_INFO *, ulong, uchar **);
#define mi_get_rec_buff_ptr(info,buf)                              \
        ((((info)->s->options & HA_OPTION_PACK_RECORD) && (buf)) ? \
        (buf) - MI_REC_BUFF_OFFSET : (buf))
#define mi_get_rec_buff_len(info,buf)                              \
        (*((uint32 *)(mi_get_rec_buff_ptr(info,buf))))

extern size_t _mi_rec_unpack(MI_INFO *info, uchar *to, uchar *from,
                            ulong reclength);
extern my_bool _mi_rec_check(MI_INFO *info,const uchar *record, uchar *packpos,
                             ulong packed_length, my_bool with_checkum);
extern int _mi_write_part_record(MI_INFO *info, my_off_t filepos, ulong length,
                                 my_off_t next_filepos, uchar ** record,
                                 ulong *reclength, int *flag);
extern void _mi_print_key(FILE *stream, HA_KEYSEG *keyseg, const uchar *key,
                          uint length);
extern my_bool _mi_read_pack_info(MI_INFO *info, pbool fix_keys);
extern int _mi_read_pack_record(MI_INFO *info, my_off_t filepos, uchar *buf);
extern int _mi_read_rnd_pack_record(MI_INFO *, uchar *, my_off_t, my_bool);
extern int _mi_pack_rec_unpack(MI_INFO *info, MI_BIT_BUFF *bit_buff,
                               uchar *to, uchar *from, ulong reclength);
extern ulonglong mi_safe_mul(ulonglong a, ulonglong b);
extern int _mi_ft_update(MI_INFO *info, uint keynr, uchar *keybuf,
                         const uchar *oldrec, const uchar *newrec,
                         my_off_t pos);
extern my_bool mi_yield_and_check_if_killed(MI_INFO *info, int inx);
extern my_bool mi_killed_standalone(MI_INFO *);

struct st_sort_info;


typedef struct st_mi_block_info         /* Parameter to _mi_get_block_info */
{
  uchar header[MI_BLOCK_INFO_HEADER_LENGTH];
  ulong rec_len;
  ulong data_len;
  ulong block_len;
  ulong blob_len;
  my_off_t filepos;
  my_off_t next_filepos;
  my_off_t prev_filepos;
  uint second_read;
  uint offset;
} MI_BLOCK_INFO;


struct st_sort_key_blocks		/* Used when sorting */
{
  uchar *buff, *end_pos;
  uchar lastkey[HA_MAX_POSSIBLE_KEY_BUFF];
  uint last_length;
  int inited;
};


struct st_sort_ftbuf
{
  uchar *buf, *end;
  int count;
  uchar lastkey[HA_MAX_KEY_BUFF];
};

/* bits in return from _mi_get_block_info */

#define BLOCK_FIRST     1U
#define BLOCK_LAST      2U
#define BLOCK_DELETED   4U
#define BLOCK_ERROR     8U              /* Wrong data */
#define BLOCK_SYNC_ERROR 16U            /* Right data at wrong place */
#define BLOCK_FATAL_ERROR 32U           /* hardware-error */

#define NEED_MEM        ((uint) 10*4*(IO_SIZE+32)+32) /* Nead for recursion */
#define MAXERR                  20
#define BUFFERS_WHEN_SORTING    16      /* Alloc for sort-key-tree */
#define WRITE_COUNT             MY_HOW_OFTEN_TO_WRITE
#define INDEX_TMP_EXT           ".TMM"
#define DATA_TMP_EXT            ".TMD"

#define UPDATE_TIME             1U
#define UPDATE_STAT             2U
#define UPDATE_SORT             4U
#define UPDATE_AUTO_INC         8U
#define UPDATE_OPEN_COUNT       16U

/* We use MY_ALIGN_DOWN here mainly to ensure that we get stable values for mysqld --help ) */
#define KEY_BUFFER_INIT	        MY_ALIGN_DOWN(1024L*1024L-MALLOC_OVERHEAD, IO_SIZE)
#define READ_BUFFER_INIT	MY_ALIGN_DOWN(1024L*256L-MALLOC_OVERHEAD, 1024)
#define SORT_BUFFER_INIT	MY_ALIGN_DOWN(1024L*1024L*128L-MALLOC_OVERHEAD, 1024)
#define MIN_SORT_BUFFER		4096U

enum myisam_log_commands
{
  MI_LOG_OPEN, MI_LOG_WRITE, MI_LOG_UPDATE, MI_LOG_DELETE, MI_LOG_CLOSE,
    MI_LOG_EXTRA, MI_LOG_LOCK, MI_LOG_DELETE_ALL
};

#define myisam_log(a,b,c,d) if (myisam_log_file >= 0) _myisam_log(a,b,c,d)
#define myisam_log_command(a,b,c,d,e) if (myisam_log_file >= 0) _myisam_log_command(a,b,c,d,e)
#define myisam_log_record(a,b,c,d,e) if (myisam_log_file >= 0) _myisam_log_record(a,b,c,d,e)

#define fast_mi_writeinfo(INFO) if (!(INFO)->s->tot_locks) (void) _mi_writeinfo((INFO),0)
#define fast_mi_readinfo(INFO) ((INFO)->lock_type == F_UNLCK) && _mi_readinfo((INFO),F_RDLCK,1)

extern uint _mi_get_block_info(MI_BLOCK_INFO *, File, my_off_t);
extern uint _mi_rec_pack(MI_INFO *info, uchar *to, const uchar *from);
extern uint _mi_pack_get_block_info(MI_INFO *myisam, MI_BIT_BUFF *bit_buff,
                                    MI_BLOCK_INFO *info, uchar **rec_buff_p,
                                    File file, my_off_t filepos);
extern void _mi_store_blob_length(uchar *pos, uint pack_length, uint length);
extern void _myisam_log(enum myisam_log_commands command, MI_INFO *info,
                        const uchar *buffert, uint length);
extern void _myisam_log_command(enum myisam_log_commands command,
                                MI_INFO *info, const uchar *buffert,
                                uint length, int result);
extern void _myisam_log_record(enum myisam_log_commands command, MI_INFO *info,
                               const uchar *record, my_off_t filepos,
                               int result);
extern void mi_report_error(int errcode, const char *file_name);
extern my_bool _mi_memmap_file(MI_INFO *info);
extern void _mi_unmap_file(MI_INFO *info);
extern uint save_pack_length(uint version, uchar *block_buff, ulong length);
extern uint calc_pack_length(uint version, ulong length);
extern size_t mi_mmap_pread(MI_INFO *info, uchar *Buffer,
                            size_t Count, my_off_t offset, myf MyFlags);
extern size_t mi_mmap_pwrite(MI_INFO *info, const uchar *Buffer,
                             size_t Count, my_off_t offset, myf MyFlags);
extern size_t mi_nommap_pread(MI_INFO *info, uchar *Buffer,
                              size_t Count, my_off_t offset, myf MyFlags);
extern size_t mi_nommap_pwrite(MI_INFO *info, const uchar *Buffer,
                               size_t Count, my_off_t offset, myf MyFlags);

uint mi_state_info_write(File file, MI_STATE_INFO *state, uint pWrite);
uchar *mi_state_info_read(uchar *ptr, MI_STATE_INFO *state);
uint mi_state_info_read_dsk(File file, MI_STATE_INFO *state, my_bool pRead);
uint mi_base_info_write(File file, MI_BASE_INFO *base);
uchar *mi_n_base_info_read(uchar *ptr, MI_BASE_INFO *base);
int mi_keyseg_write(File file, const HA_KEYSEG *keyseg);
uchar *mi_keyseg_read(uchar *ptr, HA_KEYSEG *keyseg);
uint mi_keydef_write(File file, MI_KEYDEF *keydef);
uchar *mi_keydef_read(uchar *ptr, MI_KEYDEF *keydef);
uint mi_uniquedef_write(File file, MI_UNIQUEDEF *keydef);
uchar *mi_uniquedef_read(uchar *ptr, MI_UNIQUEDEF *keydef);
uint mi_recinfo_write(File file, MI_COLUMNDEF *recinfo);
uchar *mi_recinfo_read(uchar *ptr, MI_COLUMNDEF *recinfo);
extern int mi_disable_indexes(MI_INFO *info);
extern int mi_enable_indexes(MI_INFO *info);
extern int mi_indexes_are_disabled(MI_INFO *info);
ulong _mi_calc_total_blob_length(MI_INFO *info, const uchar *record);
ha_checksum mi_checksum(MI_INFO *info, const uchar *buf);
ha_checksum mi_static_checksum(MI_INFO *info, const uchar *buf);
my_bool mi_check_unique(MI_INFO *info, MI_UNIQUEDEF *def, const uchar *record,
                        ha_checksum unique_hash, my_off_t pos);
ha_checksum mi_unique_hash(MI_UNIQUEDEF *def, const uchar *buf);
int _mi_cmp_static_unique(MI_INFO *info, MI_UNIQUEDEF *def,
                          const uchar *record, my_off_t pos);
int _mi_cmp_dynamic_unique(MI_INFO *info, MI_UNIQUEDEF *def,
                           const uchar *record, my_off_t pos);
int mi_unique_comp(MI_UNIQUEDEF *def, const uchar *a, const uchar *b,
                   my_bool null_are_equal);
my_bool mi_get_status(void *param, my_bool concurrent_insert);
void mi_update_status(void *param);
void mi_restore_status(void *param);
void mi_copy_status(void *to, void *from);
my_bool mi_check_status(void *param);
void mi_fix_status(void *org_table, void *new_table);
extern MI_INFO *test_if_reopen(char *filename);
my_bool check_table_is_closed(const char *name, const char *where);
int mi_open_datafile(MI_INFO *info, MYISAM_SHARE *share);

int mi_open_keyfile(MYISAM_SHARE *share);
void mi_setup_functions(MYISAM_SHARE *share);
my_bool mi_dynmap_file(MI_INFO *info, my_off_t size);
int mi_munmap_file(MI_INFO *info);
void mi_remap_file(MI_INFO *info, my_off_t size);

check_result_t mi_check_index_tuple_real(MI_INFO *info, uint keynr,
                                         uchar *record);
static inline check_result_t mi_check_index_tuple(MI_INFO *info, uint keynr,
                                                  uchar *record)
{
  if (!info->has_cond_pushdown && ! info->rowid_filter_func)
    return CHECK_POS;
  return mi_check_index_tuple_real(info, keynr, record);
}

    /* Functions needed by mi_check */
int killed_ptr(HA_CHECK *param);
void mi_check_print_error(HA_CHECK *param, const char *fmt, ...)
  ATTRIBUTE_FORMAT(printf, 2, 3);
void mi_check_print_warning(HA_CHECK *param, const char *fmt, ...)
  ATTRIBUTE_FORMAT(printf, 2, 3);
void mi_check_print_info(HA_CHECK *param, const char *fmt, ...)
  ATTRIBUTE_FORMAT(printf, 2, 3);
pthread_handler_t thr_find_all_keys(void *arg);
extern void mi_set_index_cond_func(MI_INFO *info, index_cond_func_t check_func,
                                   void *func_arg);
extern void mi_set_rowid_filter_func(MI_INFO *info,
                                     rowid_filter_func_t check_func,
                                     void *func_arg);
int flush_blocks(HA_CHECK *param, KEY_CACHE *key_cache, File file,
                 ulonglong *dirty_part_map);

#ifdef HAVE_PSI_INTERFACE
extern PSI_mutex_key mi_key_mutex_MYISAM_SHARE_intern_lock,
  mi_key_mutex_MI_SORT_INFO_mutex, mi_key_mutex_MI_CHECK_print_msg;

extern PSI_rwlock_key mi_key_rwlock_MYISAM_SHARE_key_root_lock,
  mi_key_rwlock_MYISAM_SHARE_mmap_lock;

extern PSI_cond_key mi_key_cond_MI_SORT_INFO_cond;

extern PSI_file_key mi_key_file_datatmp, mi_key_file_dfile, mi_key_file_kfile,
  mi_key_file_log;

extern PSI_thread_key mi_key_thread_find_all_keys;

void init_myisam_psi_keys();
#else
#define init_myisam_psi_keys() do { } while(0)
#endif /* HAVE_PSI_INTERFACE */

extern PSI_memory_key mi_key_memory_MYISAM_SHARE;
extern PSI_memory_key mi_key_memory_MI_INFO;
extern PSI_memory_key mi_key_memory_MI_INFO_ft1_to_ft2;
extern PSI_memory_key mi_key_memory_MI_INFO_bulk_insert;
extern PSI_memory_key mi_key_memory_record_buffer;
extern PSI_memory_key mi_key_memory_FTB;
extern PSI_memory_key mi_key_memory_FT_INFO;
extern PSI_memory_key mi_key_memory_FTPARSER_PARAM;
extern PSI_memory_key mi_key_memory_ft_memroot;
extern PSI_memory_key mi_key_memory_ft_stopwords;
extern PSI_memory_key mi_key_memory_MI_SORT_PARAM;
extern PSI_memory_key mi_key_memory_MI_SORT_PARAM_wordroot;
extern PSI_memory_key mi_key_memory_SORT_FT_BUF;
extern PSI_memory_key mi_key_memory_SORT_KEY_BLOCKS;
extern PSI_memory_key mi_key_memory_filecopy;
extern PSI_memory_key mi_key_memory_SORT_INFO_buffer;
extern PSI_memory_key mi_key_memory_MI_DECODE_TREE;
extern PSI_memory_key mi_key_memory_MYISAM_SHARE_decode_tables;
extern PSI_memory_key mi_key_memory_preload_buffer;
extern PSI_memory_key mi_key_memory_stPageList_pages;
extern PSI_memory_key mi_key_memory_keycache_thread_var;

C_MODE_END
