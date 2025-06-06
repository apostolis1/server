/* Copyright (c) 2002, 2013, Oracle and/or its affiliates.
   Copyright (c) 2009, 2020, MariaDB Corporation.

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

#include "strings_def.h"
#include <m_ctype.h>
#include "ctype-simple.h"
#include "my_sys.h"  /* Needed for MY_ERRNO_ERANGE */
#include <errno.h>

#include "stdarg.h"
#include "my_bit.h"

/*
  Returns the number of bytes required for strnxfrm().
*/

size_t my_strnxfrmlen_simple(CHARSET_INFO *cs, size_t len)
{
  return len * (cs->strxfrm_multiply ? cs->strxfrm_multiply : 1);
}


/*
  Converts a string into its sort key.
  
  SYNOPSIS
     my_strnxfrm_xxx()
     
  IMPLEMENTATION
     
     The my_strxfrm_xxx() function transforms a string pointed to by
     'src' with length 'srclen' according to the charset+collation 
     pair 'cs' and copies the result key into 'dest'.
     
     Comparing two strings using memcmp() after my_strnxfrm_xxx()
     is equal to comparing two original strings with my_strnncollsp_xxx().
     
     Not more than 'dstlen' bytes are written into 'dst'.
     To guarantee that the whole string is transformed, 'dstlen' must be
     at least srclen*cs->strnxfrm_multiply bytes long. Otherwise,
     consequent memcmp() may return a non-accurate result.
     
     If the source string is too short to fill whole 'dstlen' bytes,
     then the 'dest' string is padded up to 'dstlen', ensuring that:
     
       "a"  == "a "
       "a\0" < "a"
       "a\0" < "a "
     
     my_strnxfrm_simple() is implemented for 8bit charsets and
     simple collations with one-to-one string->key transformation.
     
     See also implementations for various charsets/collations in  
     other ctype-xxx.c files.
     
  RETURN
  
    Target len 'dstlen'.
  
*/


my_strnxfrm_ret_t
my_strnxfrm_simple_internal(CHARSET_INFO * cs,
                            uchar *dst, size_t dstlen, uint *nweights,
                            const uchar *src, size_t srclen)
{
  const uchar *map= cs->sort_order;
  const uchar *src0= src;
  uchar *d0= dst;
  uint frmlen;
  uint warnings= 0;
  if ((frmlen= (uint)MY_MIN(dstlen, *nweights)) > srclen)
    frmlen= (uint)srclen;
  if (frmlen < srclen)
    warnings= MY_STRNXFRM_TRUNCATED_WEIGHT_REAL_CHAR;
  *nweights-= frmlen;
  if (dst != src)
  {
    const uchar *end;
    for (end= src + frmlen; src < end;)
      *dst++= map[*src++];
    return my_strnxfrm_ret_construct(dst - d0, src - src0, warnings);
  }
  else
  {
    const uchar *end;
    for (end= dst + frmlen; dst < end; dst++)
      *dst= map[(uchar) *dst];
    return my_strnxfrm_ret_construct(dst - d0, dst - d0, warnings);
  }
  return my_strnxfrm_ret_construct(0,0,0); /*Make compiler happy */
}


my_strnxfrm_ret_t my_strnxfrm_simple(CHARSET_INFO * cs,
                                     uchar *dst, size_t dstlen, uint nweights,
                                     const uchar *src, size_t srclen,
                                     uint flags)
{
  my_strnxfrm_ret_t rc= my_strnxfrm_simple_internal(cs, dst, dstlen, &nweights,
                                                    src, srclen);
  my_strnxfrm_ret_t rcpad= my_strxfrm_pad_desc_and_reverse(cs, dst,
                                                    dst + rc.m_result_length,
                                                    dst + dstlen,
                                                    nweights, flags, 0);
  return my_strnxfrm_ret_construct(rcpad.m_result_length,
                                   rc.m_source_length_used,
                                   rc.m_warnings | rcpad.m_warnings);
}


my_strnxfrm_ret_t
my_strnxfrm_simple_nopad(CHARSET_INFO * cs,
                         uchar *dst, size_t dstlen, uint nweights,
                         const uchar *src, size_t srclen, uint flags)
{
  my_strnxfrm_ret_t rc= my_strnxfrm_simple_internal(cs, dst, dstlen, &nweights,
                                                    src, srclen);
  my_strnxfrm_ret_t rcpad= my_strxfrm_pad_desc_and_reverse_nopad(cs, dst,
                                                    dst + rc.m_result_length,
                                                    dst + dstlen,
                                                    nweights, flags, 0);
  return my_strnxfrm_ret_construct(rcpad.m_result_length,
                                   rc.m_source_length_used,
                                   rc.m_warnings | rcpad.m_warnings);
}


int my_strnncoll_simple(CHARSET_INFO * cs, const uchar *s, size_t slen, 
                        const uchar *t, size_t tlen,
                        my_bool t_is_prefix)
{
  size_t len = ( slen > tlen ) ? tlen : slen;
  const uchar *map= cs->sort_order;
  if (t_is_prefix && slen > tlen)
    slen=tlen;
  while (len--)
  {
    if (map[*s++] != map[*t++])
      return ((int) map[s[-1]] - (int) map[t[-1]]);
  }
  /*
    We can't use (slen - tlen) here as the result may be outside of the
    precision of a signed int
  */
  return slen > tlen ? 1 : slen < tlen ? -1 : 0 ;
}


/*
  Compare strings, discarding end space

  SYNOPSIS
    my_strnncollsp_simple()
    cs			character set handler
    a			First string to compare
    a_length		Length of 'a'
    b			Second string to compare
    b_length		Length of 'b'

  IMPLEMENTATION
    If one string is shorter as the other, then we space extend the other
    so that the strings have equal length.

    This will ensure that the following things hold:

    "a"  == "a "
    "a\0" < "a"
    "a\0" < "a "

  RETURN
    < 0	 a <  b
    = 0	 a == b
    > 0	 a > b
*/

int my_strnncollsp_simple(CHARSET_INFO * cs, const uchar *a, size_t a_length, 
			  const uchar *b, size_t b_length)
{
  const uchar *map= cs->sort_order, *end;
  size_t length;

  end= a + (length= MY_MIN(a_length, b_length));
  while (a < end)
  {
    if (map[*a++] != map[*b++])
      return ((int) map[a[-1]] - (int) map[b[-1]]);
  }
  if (a_length != b_length)
  {
    int swap= 1;
    /*
      Check the next not space character of the longer key. If it's < ' ',
      then it's smaller than the other key.
    */
    if (a_length < b_length)
    {
      /* put shorter key in s */
      a_length= b_length;
      a= b;
      swap= -1;                                 /* swap sign of result */
    }
    for (end= a + a_length-length; a < end ; a++)
    {
      if (map[*a] != map[' '])
        return (map[*a] < map[' ']) ? -swap : swap;
    }
  }
  return 0;
}


static int
my_strnncollsp_nchars_simple(CHARSET_INFO * cs,
                             const uchar *a, size_t a_length,
                             const uchar *b, size_t b_length,
                             size_t nchars,
                             uint flags)
{
  set_if_smaller(a_length, nchars);
  set_if_smaller(b_length, nchars);
  return my_strnncollsp_simple(cs, a, a_length, b, b_length);
}


int my_strnncollsp_simple_nopad(CHARSET_INFO * cs,
                                const uchar *a, size_t a_length,
                                const uchar *b, size_t b_length)
{
  return my_strnncoll_simple(cs, a, a_length, b, b_length, FALSE);
}


size_t my_caseup_str_8bit(CHARSET_INFO * cs,char *str)
{
  register const uchar *map= cs->to_upper;
  char *str_orig= str;
  while ((*str= (char) map[(uchar) *str]) != 0)
    str++;
  return (size_t) (str - str_orig);
}


size_t my_casedn_str_8bit(CHARSET_INFO * cs,char *str)
{
  register const uchar *map= cs->to_lower;
  char *str_orig= str;
  while ((*str= (char) map[(uchar) *str]) != 0)
    str++;
  return (size_t) (str - str_orig);
}


size_t my_caseup_8bit(CHARSET_INFO * cs, const char *src, size_t srclen,
                      char *dst, size_t dstlen __attribute__((unused)))
{
  const char *end= src + srclen;
  register const uchar *map= cs->to_upper;
  DBUG_ASSERT(src != NULL); /* Avoid UBSAN nullptr-with-offset */
  DBUG_ASSERT(srclen <= dstlen);
  for ( ; src != end ; src++)
    *dst++= (char) map[(uchar) *src];
  return srclen;
}


size_t my_casedn_8bit(CHARSET_INFO * cs, const char *src, size_t srclen,
                      char *dst, size_t dstlen __attribute__((unused)))
{
  const char *end= src + srclen;
  register const uchar *map=cs->to_lower;
  DBUG_ASSERT(src != NULL); /* Avoid UBSAN nullptr-with-offset */
  DBUG_ASSERT(srclen <= dstlen);
  for ( ; src != end ; src++)
    *dst++= (char) map[(uchar) *src];
  return srclen;
}

int my_strcasecmp_8bit(CHARSET_INFO * cs,const char *s, const char *t)
{
  register const uchar *map=cs->to_upper;
  while (map[(uchar) *s] == map[(uchar) *t++])
    if (!*s++) return 0;
  return ((int) map[(uchar) s[0]] - (int) map[(uchar) t[-1]]);
}


int my_charlen_8bit(CHARSET_INFO *cs __attribute__((unused)),
                    const uchar *str, const uchar *end)
{
  return str >= end ? MY_CS_TOOSMALL : 1;
}


int my_mb_wc_8bit(CHARSET_INFO *cs,my_wc_t *wc,
		  const uchar *str,
		  const uchar *end __attribute__((unused)))
{
  if (str >= end)
    return MY_CS_TOOSMALL;
  
  *wc=cs->tab_to_uni[*str];
  return (!wc[0] && str[0]) ? -1 : 1;
}

int my_wc_mb_8bit(CHARSET_INFO *cs,my_wc_t wc,
		  uchar *str,
		  uchar *end)
{
  MY_UNI_IDX *idx;

  if (str >= end)
    return MY_CS_TOOSMALL;
  
  for (idx=cs->tab_from_uni; idx->tab ; idx++)
  {
    if (idx->from <= wc && idx->to >= wc)
    {
      str[0]= idx->tab[wc - idx->from];
      return (!str[0] && wc) ? MY_CS_ILUNI : 1;
    }
  }
  return MY_CS_ILUNI;
}


/* 
   We can't use vsprintf here as it's not guaranteed to return
   the length on all operating systems.
   This function is also not called in a safe environment, so the
   end buffer must be checked.
*/

size_t my_snprintf_8bit(CHARSET_INFO *cs  __attribute__((unused)),
                        char* to, size_t n  __attribute__((unused)),
		     const char* fmt, ...)
{
  va_list args;
  size_t result;
  va_start(args,fmt);
  result= my_vsnprintf(to, n, fmt, args);
  va_end(args);
  return result;
}


void my_hash_sort_simple_nopad(CHARSET_INFO *cs,
			       const uchar *key, size_t len,
			       ulong *nr1, ulong *nr2)
{
  register const uchar *sort_order=cs->sort_order;
  const uchar *end= key + len;
  register ulong m1= *nr1, m2= *nr2;
  DBUG_ASSERT(key); /* Avoid UBSAN nullptr-with-offset */
  for (; key < (uchar*) end ; key++)
  {
    MY_HASH_ADD(m1, m2, (uint) sort_order[(uint) *key]);
  }
  *nr1= m1;
  *nr2= m2;
}


void my_hash_sort_simple(CHARSET_INFO *cs,
                         const uchar *key, size_t len,
                         ulong *nr1, ulong *nr2)
{
  register const uchar *sort_order=cs->sort_order;
  const uchar *end;
  uint16 space_weight= sort_order[' '];
  DBUG_ASSERT(key); /* Avoid UBSAN nullptr-with-offset */

  /*
    Remove all trailing characters that are equal to space.
    We have to do this to be able to compare 'A ' and 'A' as identical.

    If the key is long enough, cut the trailing spaces (0x20) using an
    optimized function implemented in skip_trailing_spaces().

    "len > 16" is just some heuristic here.
    Calling skip_triling_space() for short values is not desirable,
    because its initialization block may be more expensive than the
    performance gained.
  */

  end= len > 16 ? skip_trailing_space(key, len) : key + len;

  /*
    We removed all trailing characters that are binary equal to space 0x20.
    Now remove all trailing characters that have weights equal to space.
    Some 8bit simple collations may have such characters:
    - cp1250_general_ci    0xA0 NO-BREAK SPACE == 0x20 SPACE
    - cp1251_ukrainian_ci  0x60 GRAVE ACCENT   == 0x20 SPACE
    - koi8u_general_ci     0x60 GRAVE ACCENT   == 0x20 SPACE
  */

  for ( ; key < end ; )
  {
    if (sort_order[*--end] != space_weight)
    {
      end++;
      break;
    }
  }
  my_hash_sort_simple_nopad(cs, key, end - key, nr1, nr2);
}


long my_strntol_8bit(CHARSET_INFO *cs,
		     const char *nptr, size_t l, int base,
		     char **endptr, int *err)
{
  int negative;
  register uint32 cutoff;
  register uint cutlim;
  register uint32 i;
  register const char *s;
  register uchar c;
  const char *save, *e;
  int overflow;

  *err= 0;				/* Initialize error indicator */

  s = nptr;
  e = nptr+l;
  
  for ( ; s<e && my_isspace(cs, *s) ; s++);
  
  if (s == e)
  {
    goto noconv;
  }
  
  /* Check for a sign.	*/
  if (*s == '-')
  {
    negative = 1;
    ++s;
  }
  else if (*s == '+')
  {
    negative = 0;
    ++s;
  }
  else
    negative = 0;

  save = s;
  cutoff = ((uint32)~0L) / (uint32) base;
  cutlim = (uint) (((uint32)~0L) % (uint32) base);

  overflow = 0;
  i = 0;
  for (c = *s; s != e; c = *++s)
  {
    if (c>='0' && c<='9')
      c -= '0';
    else if (c>='A' && c<='Z')
      c = c - 'A' + 10;
    else if (c>='a' && c<='z')
    {
      c = c - 'a' + 10;
      if (base > 36)
        c += 26;
    }
    else
      break;
    if (c >= base)
      break;
    if (i > cutoff || (i == cutoff && c > cutlim))
      overflow = 1;
    else
    {
      i *= (uint32) base;
      i += c;
    }
  }
  
  if (s == save)
    goto noconv;
  
  if (endptr != NULL)
    *endptr = (char *) s;
  
  if (negative)
  {
    if (i  > (uint32) INT_MIN32)
      overflow = 1;
  }
  else if (i > INT_MAX32)
    overflow = 1;
  
  if (overflow)
  {
    err[0]= ERANGE;
    return negative ? INT_MIN32 : INT_MAX32;
  }
  
  return (negative ? -((long) i) : (long) i);

noconv:
  err[0]= EDOM;
  if (endptr != NULL)
    *endptr = (char *) nptr;
  return 0L;
}


ulong my_strntoul_8bit(CHARSET_INFO *cs,
		       const char *nptr, size_t l, int base,
		       char **endptr, int *err)
{
  int negative;
  register uint32 cutoff;
  register uint cutlim;
  register uint32 i;
  register const char *s;
  const char *save, *e;
  int overflow;

  *err= 0;				/* Initialize error indicator */

  s = nptr;
  e = nptr+l;
  
  for( ; s<e && my_isspace(cs, *s); s++);
  
  if (s==e)
  {
    goto noconv;
  }

  if (*s == '-')
  {
    negative = 1;
    ++s;
  }
  else if (*s == '+')
  {
    negative = 0;
    ++s;
  }
  else
    negative = 0;

  save = s;
  cutoff = ((uint32)~0L) / (uint32) base;
  cutlim = (uint) (((uint32)~0L) % (uint32) base);
  overflow = 0;
  i = 0;
  
  for ( ; s != e; ++s)
  {
    register uchar c= *s;
    if (c>='0' && c<='9')
      c -= '0';
    else if (c>='A' && c<='Z')
      c = c - 'A' + 10;
    else if (c>='a' && c<='z')
    {
      c = c - 'a' + 10;
      if (base > 36)
        c += 26;
    }
    else
      break;
    if (c >= base)
      break;
    if (i > cutoff || (i == cutoff && c > cutlim))
      overflow = 1;
    else
    {
      i *= (uint32) base;
      i += c;
    }
  }

  if (s == save)
    goto noconv;

  if (endptr != NULL)
    *endptr = (char *) s;

  if (overflow)
  {
    err[0]= ERANGE;
    return (~(uint32) 0);
  }
  
  return (negative ? -((long) i) : (long) i);
  
noconv:
  err[0]= EDOM;
  if (endptr != NULL)
    *endptr = (char *) nptr;
  return 0L;
}


longlong my_strntoll_8bit(CHARSET_INFO *cs __attribute__((unused)),
			  const char *nptr, size_t l, int base,
			  char **endptr,int *err)
{
  int negative;
  register ulonglong cutoff;
  register uint cutlim;
  register ulonglong i;
  register const char *s, *e;
  const char *save;
  int overflow;

  *err= 0;				/* Initialize error indicator */

  s = nptr;
  e = nptr+l;

  for(; s<e && my_isspace(cs,*s); s++);

  if (s == e)
  {
    goto noconv;
  }

  if (*s == '-')
  {
    negative = 1;
    ++s;
  }
  else if (*s == '+')
  {
    negative = 0;
    ++s;
  }
  else
    negative = 0;

  save = s;

  cutoff = (~(ulonglong) 0) / (unsigned long int) base;
  cutlim = (uint) ((~(ulonglong) 0) % (unsigned long int) base);

  overflow = 0;
  i = 0;
  for ( ; s != e; s++)
  {
    register uchar c= *s;
    if (c>='0' && c<='9')
      c -= '0';
    else if (c>='A' && c<='Z')
      c = c - 'A' + 10;
    else if (c>='a' && c<='z')
    {
      c = c - 'a' + 10;
      if (base > 36)
        c += 26;
    }
    else
      break;
    if (c >= base)
      break;
    if (i > cutoff || (i == cutoff && c > cutlim))
      overflow = 1;
    else
    {
      i *= (ulonglong) base;
      i += c;
    }
  }

  if (s == save)
    goto noconv;

  if (endptr != NULL)
    *endptr = (char *) s;

  if (negative)
  {
    if (i >= (ulonglong) LONGLONG_MIN)
    {
      if (i == (ulonglong) LONGLONG_MIN)
        return LONGLONG_MIN;
      overflow = 1;
    }
  }
  else if (i > (ulonglong) LONGLONG_MAX)
    overflow = 1;

  if (overflow)
  {
    err[0]= ERANGE;
    return negative ? LONGLONG_MIN : LONGLONG_MAX;
  }

  return (negative ? -((longlong) i) : (longlong) i);

noconv:
  err[0]= EDOM;
  if (endptr != NULL)
    *endptr = (char *) nptr;
  return 0L;
}


ulonglong my_strntoull_8bit(CHARSET_INFO *cs,
			   const char *nptr, size_t l, int base,
			   char **endptr, int *err)
{
  int negative;
  register ulonglong cutoff;
  register uint cutlim;
  register ulonglong i;
  register const char *s, *e;
  const char *save;
  int overflow;

  *err= 0;				/* Initialize error indicator */

  s = nptr;
  e = nptr+l;

  for(; s<e && my_isspace(cs,*s); s++);

  if (s == e)
  {
    goto noconv;
  }

  if (*s == '-')
  {
    negative = 1;
    ++s;
  }
  else if (*s == '+')
  {
    negative = 0;
    ++s;
  }
  else
    negative = 0;

  save = s;

  cutoff = (~(ulonglong) 0) / (unsigned long int) base;
  cutlim = (uint) ((~(ulonglong) 0) % (unsigned long int) base);

  overflow = 0;
  i = 0;
  for ( ; s != e; s++)
  {
    register uchar c= *s;

    if (c>='0' && c<='9')
      c -= '0';
    else if (c>='A' && c<='Z')
      c = c - 'A' + 10;
    else if (c>='a' && c<='z')
    {
      c = c - 'a' + 10;
      if (base > 36)
        c += 26;
    }
    else
      break;
    if (c >= base)
      break;
    if (i > cutoff || (i == cutoff && c > cutlim))
      overflow = 1;
    else
    {
      i *= (ulonglong) base;
      i += c;
    }
  }

  if (s == save)
    goto noconv;

  if (endptr != NULL)
    *endptr = (char *) s;

  if (overflow)
  {
    err[0]= ERANGE;
    return (~(ulonglong) 0);
  }

  /* Avoid undefined behavior - negation of LONGLONG_MIN */
  return negative && (longlong) i != LONGLONG_MIN ?
        -((longlong) i) :
         (longlong) i;

noconv:
  err[0]= EDOM;
  if (endptr != NULL)
    *endptr = (char *) nptr;
  return 0L;
}


/*
  Read double from string

  SYNOPSIS:
    my_strntod_8bit()
    cs		Character set information
    str		String to convert to double
    length	Optional length for string.
    end		result pointer to end of converted string
    err		Error number if failed conversion
    
  NOTES:
    If length is not INT_MAX32 or str[length] != 0 then the given str must
    be writeable
    If length == INT_MAX32 the str must be \0 terminated.

    It's implemented this way to save a buffer allocation and a memory copy.

  RETURN
    Value of number in string
*/


double my_strntod_8bit(CHARSET_INFO *cs __attribute__((unused)),
		       char *str, size_t length,
		       char **end, int *err)
{
  if (length == INT_MAX32)
    length= 65535;                          /* Should be big enough */
  *end= str + length;
  return my_strtod(str, end, err);
}


/*
  This is a fast version optimized for the case of radix 10 / -10

  Assume len >= 1
*/

size_t my_long10_to_str_8bit(CHARSET_INFO *cs __attribute__((unused)),
                             char *dst, size_t len, int radix, long int val)
{
  char buffer[66];
  register char *p, *e;
  long int new_val;
  uint sign=0;
  unsigned long int uval = (unsigned long int) val;

  e = p = &buffer[sizeof(buffer)-1];
  *p= 0;
  
  if (radix < 0)
  {
    if (val < 0)
    {
      /* Avoid integer overflow in (-val) for LONGLONG_MIN (BUG#31799). */
      uval= (unsigned long int)0 - uval;
      *dst++= '-';
      len--;
      sign= 1;
    }
  }
  
  new_val = (long) (uval / 10);
  *--p    = '0'+ (char) (uval - (unsigned long) new_val * 10);
  val     = new_val;
  
  while (val != 0)
  {
    new_val=val/10;
    *--p = '0' + (char) (val-new_val*10);
    val= new_val;
  }
  
  len= MY_MIN(len, (size_t) (e-p));
  memcpy(dst, p, len);
  return len+sign;
}


size_t my_longlong10_to_str_8bit(CHARSET_INFO *cs __attribute__((unused)),
                                 char *dst, size_t len, int radix,
                                 longlong val)
{
  char buffer[65];
  register char *p, *e;
  long long_val;
  uint sign= 0;
  ulonglong uval = (ulonglong)val;
  
  if (radix < 0)
  {
    if (val < 0)
    {
      /* Avoid integer overflow in (-val) for LONGLONG_MIN (BUG#31799). */
      uval = (ulonglong)0 - uval;
      *dst++= '-';
      len--;
      sign= 1;
    }
  }
  
  e = p = &buffer[sizeof(buffer)-1];
  *p= 0;
  
  if (uval == 0)
  {
    *--p= '0';
    len= 1;
    goto cnv;
  }
  
  while (uval > (ulonglong) LONG_MAX)
  {
    ulonglong quo= uval/(uint) 10;
    uint rem= (uint) (uval- quo* (uint) 10);
    *--p = '0' + rem;
    uval= quo;
  }
  
  long_val= (long) uval;
  while (long_val != 0)
  {
    long quo= long_val/10;
    *--p = (char) ('0' + (long_val - quo*10));
    long_val= quo;
  }
  
  len= MY_MIN(len, (size_t) (e-p));
cnv:
  memcpy(dst, p, len);
  return len+sign;
}


size_t my_min_str_8bit_simple(CHARSET_INFO *cs,
                              uchar *dst, size_t dst_size,
                              size_t nchars)
{
  set_if_smaller(dst_size, nchars);
  memset(dst, cs->min_sort_char, dst_size);
  return dst_size;
}


size_t my_min_str_8bit_simple_nopad(CHARSET_INFO *cs,
                                    uchar *dst, size_t dst_size,
                                    size_t nchars)
{
  /* For NOPAD collations, the empty string is always the smallest */
  return 0;
}


size_t my_max_str_8bit_simple(CHARSET_INFO *cs,
                              uchar *dst, size_t dst_size,
                              size_t nchars)
{
  set_if_smaller(dst_size, nchars);
  memset(dst, cs->max_sort_char, dst_size);
  return dst_size;
}


/*
** Compare string against string with wildcard
**	0 if matched
**	-1 if not matched with wildcard
**	 1 if matched with wildcard
*/

#ifdef LIKE_CMP_TOUPPER
#define likeconv(s,A) (uchar) my_toupper(s,A)
#else
#define likeconv(s,A) (uchar) (s)->sort_order[(uchar) (A)]
#endif

#define INC_PTR(cs,A,B) (A)++


static
int my_wildcmp_8bit_impl(CHARSET_INFO *cs,
                         const char *str,const char *str_end,
                         const char *wildstr,const char *wildend,
                         int escape, int w_one, int w_many, int recurse_level)
{
  int result= -1;			/* Not found, using wildcards */

  if (my_string_stack_guard && my_string_stack_guard(recurse_level))
    return 1;
  while (wildstr != wildend)
  {
    while (*wildstr != w_many && *wildstr != w_one)
    {
      if (*wildstr == escape && wildstr+1 != wildend)
	wildstr++;

      if (str == str_end || likeconv(cs,*wildstr++) != likeconv(cs,*str++))
	return(1);				/* No match */
      if (wildstr == wildend)
	return(str != str_end);		/* Match if both are at end */
      result=1;					/* Found an anchor char     */
    }
    if (*wildstr == w_one)
    {
      do
      {
	if (str == str_end)			/* Skip one char if possible */
	  return(result);
	INC_PTR(cs,str,str_end);
      } while (++wildstr < wildend && *wildstr == w_one);
      if (wildstr == wildend)
	break;
    }
    if (*wildstr == w_many)
    {						/* Found w_many */
      uchar cmp;
      
      wildstr++;
      /* Remove any '%' and '_' from the wild search string */
      for (; wildstr != wildend ; wildstr++)
      {
	if (*wildstr == w_many)
	  continue;
	if (*wildstr == w_one)
	{
	  if (str == str_end)
	    return(-1);
	  INC_PTR(cs,str,str_end);
	  continue;
	}
	break;					/* Not a wild character */
      }
      if (wildstr == wildend)
	return(0);				/* Ok if w_many is last */
      if (str == str_end)
	return(-1);
      
      if ((cmp= *wildstr) == escape && wildstr+1 != wildend)
	cmp= *++wildstr;

      INC_PTR(cs,wildstr,wildend);	/* This is compared trough cmp */
      cmp=likeconv(cs,cmp);
      do
      {
        /*
          Find the next character in the subject string equal to 'cmp', then
          check recursively my_wildcmp_8bit_impl() for the pattern remainder.
        */
	while (str != str_end && (uchar) likeconv(cs,*str) != cmp)
	  str++;
	if (str++ == str_end)
	  return(-1); /* 'cmp' was not found in the subject string */
	{
	  int tmp=my_wildcmp_8bit_impl(cs,str,str_end,
                                       wildstr,wildend,escape,w_one,
                                       w_many, recurse_level+1);
	  if (tmp <= 0)
	    return(tmp);
	}
        /*
          The recursion call did not match. But it returned 1, which means
          the pattern remainder has some non-special characters.
          Continue, there is a chance that we'll find another 'cmp'
          at a different position in the subject string.
        */
      } while (str != str_end);
      return(-1);
    }
  }
  return(str != str_end ? 1 : 0);
}

int my_wildcmp_8bit(CHARSET_INFO *cs,
                    const char *str,const char *str_end,
                    const char *wildstr,const char *wildend,
                    int escape, int w_one, int w_many)
{
  return my_wildcmp_8bit_impl(cs, str, str_end,
                              wildstr, wildend,
                              escape, w_one, w_many, 1);
}


/*
** Calculate min_str and max_str that ranges a LIKE string.
** Arguments:
** ptr		Pointer to LIKE string.
** ptr_length	Length of LIKE string.
** escape	Escape character in LIKE.  (Normally '\').
**		All escape characters should be removed from min_str and max_str
** res_length	Length of min_str and max_str.
** min_str	Smallest case sensitive string that ranges LIKE.
**		Should be space padded to res_length.
** max_str	Largest case sensitive string that ranges LIKE.
**		Normally padded with the biggest character sort value.
**
** The function should return 0 if ok and 1 if the LIKE string can't be
** optimized !
*/

my_bool my_like_range_simple(CHARSET_INFO *cs,
			     const char *ptr, size_t ptr_length,
			     pbool escape, pbool w_one, pbool w_many,
			     size_t res_length,
			     char *min_str,char *max_str,
			     size_t *min_length, size_t *max_length)
{
  const char *end= ptr + ptr_length;
  char *min_org=min_str;
  char *min_end=min_str+res_length;
  size_t charlen= res_length / cs->mbmaxlen;

  for (; ptr != end && min_str != min_end && charlen > 0 ; ptr++, charlen--)
  {
    if (*ptr == escape && ptr+1 != end)
    {
      ptr++;					/* Skip escape */
      *min_str++= *max_str++ = *ptr;
      continue;
    }
    if (*ptr == w_one)				/* '_' in SQL */
    {
      *min_str++='\0';				/* This should be min char */
      *max_str++= (char) cs->max_sort_char;
      continue;
    }
    if (*ptr == w_many)				/* '%' in SQL */
    {
      /* Calculate length of keys */
      *min_length= (cs->state & (MY_CS_BINSORT | MY_CS_NOPAD)) ?
                    (size_t) (min_str - min_org) :
                    res_length;
      *max_length= res_length;
      do
      {
	*min_str++= 0;
	*max_str++= (char) cs->max_sort_char;
      } while (min_str != min_end);
      return 0;
    }
    *min_str++= *max_str++ = *ptr;
  }

 *min_length= *max_length = (size_t) (min_str - min_org);
  while (min_str != min_end)
    *min_str++= *max_str++ = ' ';      /* Because if key compression */
  return 0;
}


size_t my_scan_8bit(CHARSET_INFO *cs, const char *str, const char *end, int sq)
{
  const char *str0= str;
  switch (sq)
  {
  case MY_SEQ_INTTAIL:
    if (*str == '.')
    {
      for(str++ ; str != end && *str == '0' ; str++);
      return (size_t) (str - str0);
    }
    return 0;

  case MY_SEQ_SPACES:
    for ( ; str < end ; str++)
    {
      if (!my_isspace(cs,*str))
        break;
    }
    return (size_t) (str - str0);
  case MY_SEQ_NONSPACES:
    for ( ; str < end ; str++)
    {
      if (my_isspace(cs, *str))
        break;
    }
    return (size_t) (str - str0);
  default:
    return 0;
  }
}


void my_fill_8bit(CHARSET_INFO *cs __attribute__((unused)),
		   char *s, size_t l, int fill)
{
  bfill((uchar*) s,l,fill);
}


size_t my_numchars_8bit(CHARSET_INFO *cs __attribute__((unused)),
		      const char *b, const char *e)
{
  return (size_t) (e - b);
}


size_t my_numcells_8bit(CHARSET_INFO *cs __attribute__((unused)),
                        const char *b, const char *e)
{
  return (size_t) (e - b);
}


size_t my_charpos_8bit(CHARSET_INFO *cs __attribute__((unused)),
                       const char *b  __attribute__((unused)),
                       const char *e  __attribute__((unused)),
                       size_t pos)
{
  return pos;
}


size_t
my_well_formed_char_length_8bit(CHARSET_INFO *cs __attribute__((unused)),
                                const char *start, const char *end,
                                size_t nchars, MY_STRCOPY_STATUS *status)
{
  size_t nbytes= (size_t) (end - start);
  size_t res= MY_MIN(nbytes, nchars);
  status->m_well_formed_error_pos= NULL;
  status->m_source_end_pos= start + res;
  return res;
}


/*
  Copy a 8-bit string. Not more than "nchars" character are copied.
*/
size_t
my_copy_8bit(CHARSET_INFO *cs __attribute__((unused)),
             char *dst, size_t dst_length,
             const char *src, size_t src_length,
             size_t nchars, MY_STRCOPY_STATUS *status)
{
  set_if_smaller(src_length, dst_length);
  set_if_smaller(src_length, nchars);
  if (src_length)
    memmove(dst, src, src_length);
  status->m_source_end_pos= src + src_length;
  status->m_well_formed_error_pos= NULL;   
  return src_length;
}


size_t my_lengthsp_8bit(CHARSET_INFO *cs __attribute__((unused)),
                        const char *ptr, size_t length)
{
  const char *end;
  end= (const char *) skip_trailing_space((const uchar *)ptr, length);
  return (size_t) (end-ptr);
}


uint my_instr_simple(CHARSET_INFO *cs,
                     const char *b, size_t b_length, 
                     const char *s, size_t s_length,
                     my_match_t *match, uint nmatch)
{
  register const uchar *str, *search, *end, *search_end;
  
  if (s_length <= b_length)
  {
    if (!s_length)
    {
      if (nmatch)
      {
        match->beg= 0;
        match->end= 0;
        match->mb_len= 0;
      }
      return 1;		/* Empty string is always found */
    }
    
    str= (const uchar*) b;
    search= (const uchar*) s;
    end= (const uchar*) b+b_length-s_length+1;
    search_end= (const uchar*) s + s_length;
    
skip:
    while (str != end)
    {
      if (cs->sort_order[*str++] == cs->sort_order[*search])
      {
	register const uchar *i,*j;
	
	i= str; 
	j= search+1;
	
	while (j != search_end)
	  if (cs->sort_order[*i++] != cs->sort_order[*j++]) 
            goto skip;
        
	if (nmatch > 0)
	{
	  match[0].beg= 0;
	  match[0].end= (uint) (str- (const uchar*)b-1);
	  match[0].mb_len= match[0].end;
	  
	  if (nmatch > 1)
	  {
	    match[1].beg= match[0].end;
	    match[1].end= (uint)(match[0].end+s_length);
	    match[1].mb_len= match[1].end-match[1].beg;
	  }
	}
	return 2;
      }
    }
  }
  return 0;
}


typedef struct
{
  int		nchars;
  struct my_uni_idx_st uidx;
} uni_idx;

#define PLANE_SIZE	0x100
#define PLANE_NUM	0x100
#define PLANE_NUMBER(x)	(((x)>>8) % PLANE_NUM)

static int pcmp(const void * f, const void * s)
{
  const uni_idx *F= (const uni_idx*) f;
  const uni_idx *S= (const uni_idx*) s;
  int res;

  if (!(res=((S->nchars)-(F->nchars))))
    res=((F->uidx.from)-(S->uidx.to));
  return res;
}

static my_bool
create_fromuni(struct charset_info_st *cs,
               MY_CHARSET_LOADER *loader)
{
  uni_idx	idx[PLANE_NUM];
  int		i,n;
  
  /*
    Check that Unicode map is loaded.
    It can be not loaded when the collation is
    listed in Index.xml but not specified
    in the character set specific XML file.
  */
  if (!cs->tab_to_uni)
    return TRUE;
  
  /* Clear plane statistics */
  bzero(idx,sizeof(idx));
  
  /* Count number of characters in each plane */
  for (i=0; i< 0x100; i++)
  {
    uint16 wc=cs->tab_to_uni[i];
    int pl= PLANE_NUMBER(wc);
    
    if (wc || !i)
    {
      if (!idx[pl].nchars)
      {
        idx[pl].uidx.from=wc;
        idx[pl].uidx.to=wc;
      }else
      {
        idx[pl].uidx.from=wc<idx[pl].uidx.from?wc:idx[pl].uidx.from;
        idx[pl].uidx.to=wc>idx[pl].uidx.to?wc:idx[pl].uidx.to;
      }
      idx[pl].nchars++;
    }
  }
  
  /* Sort planes in descending order */
  qsort(&idx,PLANE_NUM,sizeof(uni_idx),&pcmp);
  
  for (i=0; i < PLANE_NUM; i++)
  {
    int ch,numchars;
    uchar *tab;
    
    /* Skip empty plane */
    if (!idx[i].nchars)
      break;
    
    numchars=idx[i].uidx.to-idx[i].uidx.from+1;
    if (!(idx[i].uidx.tab= tab= (uchar*)
                                (loader->once_alloc) (numchars *
                                                      sizeof(*idx[i].uidx.tab))))
      return TRUE;
    
    bzero(tab,numchars*sizeof(*tab));
    
    for (ch=1; ch < PLANE_SIZE; ch++)
    {
      uint16 wc=cs->tab_to_uni[ch];
      if (wc >= idx[i].uidx.from && wc <= idx[i].uidx.to && wc)
      {
        int ofs= wc - idx[i].uidx.from;
        if (!tab[ofs] || tab[ofs] > 0x7F) /* Prefer ASCII*/
        {
          /*
            Some character sets can have double encoding. For example,
            in ARMSCII8, the following characters are encoded twice:

            Encoding#1 Encoding#2 Unicode Character Name
            ---------- ---------- ------- --------------
            0x27       0xFF       U+0027  APOSTROPHE
            0x28       0xA5       U+0028  LEFT PARENTHESIS
            0x29       0xA4       U+0029  RIGHT PARENTHESIS
            0x2C       0xAB       U+002C  COMMA
            0x2D       0xAC       U+002D  HYPHEN-MINUS
            0x2E       0xA9       U+002E  FULL STOP

            That is, both 0x27 and 0xFF convert to Unicode U+0027.
            When converting back from Unicode to ARMSCII,
            we prefer the ASCII range, that is we want U+0027
            to convert to 0x27 rather than to 0xFF.
          */
          tab[ofs]= ch;
        }
      }
    }
  }
  
  /* Allocate and fill reverse table for each plane */
  n=i;
  if (!(cs->tab_from_uni= (MY_UNI_IDX *)
                          (loader->once_alloc)(sizeof(MY_UNI_IDX) * (n + 1))))
    return TRUE;

  for (i=0; i< n; i++)
    ((struct my_uni_idx_st*)cs->tab_from_uni)[i]= idx[i].uidx;
  
  /* Set end-of-list marker */
  bzero((char*) &cs->tab_from_uni[i],sizeof(MY_UNI_IDX));
  return FALSE;
}


/*
  Detect if a character set is 8bit,
  and it is pure ascii, i.e. doesn't have
  characters outside U+0000..U+007F
  This functions is shared between "conf_to_src"
  and dynamic charsets loader in "mysqld".
*/
static my_bool
my_charset_is_8bit_pure_ascii(CHARSET_INFO *cs)
{
  size_t code;
  if (!cs->tab_to_uni)
    return 0;
  for (code= 0; code < 256; code++)
  {
    if (cs->tab_to_uni[code] > 0x7F)
      return 0;
  }
  return 1;
}


/*
  Shared function between conf_to_src and mysys.
  Check if a 8bit character set is compatible with
  ascii on the range 0x00..0x7F.
*/
static my_bool
my_charset_is_ascii_compatible(CHARSET_INFO *cs)
{
  uint i;
  if (!cs->tab_to_uni)
    return 1;
  for (i= 0; i < 128; i++)
  {
    if (cs->tab_to_uni[i] != i)
      return 0;
  }
  return 1;
}


uint my_8bit_charset_flags_from_data(CHARSET_INFO *cs)
{
  uint flags= 0;
  if (my_charset_is_8bit_pure_ascii(cs))
    flags|= MY_CS_PUREASCII;
  if (!my_charset_is_ascii_compatible(cs))
    flags|= MY_CS_NONASCII;
  return flags;
}


/*
  Check if case sensitive sort order: A < a < B.
  We need MY_CS_FLAG for regex library, and for
  case sensitivity flag for 5.0 client protocol,
  to support isCaseSensitive() method in JDBC driver
*/
uint my_8bit_collation_flags_from_data(CHARSET_INFO *cs)
{
  uint flags= 0;
  if (cs->sort_order && cs->sort_order['A'] < cs->sort_order['a'] &&
                        cs->sort_order['a'] < cs->sort_order['B'])
    flags|= MY_CS_CSSORT;
  return flags;
}


static my_bool
my_cset_init_8bit(struct charset_info_st *cs, MY_CHARSET_LOADER *loader)
{
  cs->state|= my_8bit_charset_flags_from_data(cs);
  cs->pad_char= ' ';
  if (!cs->to_lower || !cs->to_upper || !cs->m_ctype || !cs->tab_to_uni)
    return TRUE;
  return create_fromuni(cs, loader);
}

static void set_max_sort_char(struct charset_info_st *cs)
{
  uchar max_char;
  uint  i;
  
  if (!cs->sort_order)
    return;
  
  max_char=cs->sort_order[(uchar) cs->max_sort_char];
  for (i= 0; i < 256; i++)
  {
    if ((uchar) cs->sort_order[i] > max_char)
    {
      max_char=(uchar) cs->sort_order[i];
      cs->max_sort_char= i;
    }
  }
}

static my_bool my_coll_init_simple(struct charset_info_st *cs,
                                   MY_CHARSET_LOADER *loader __attribute__((unused)))
{
  if (!cs->sort_order)
    return TRUE;
  cs->state|= my_8bit_collation_flags_from_data(cs);
  set_max_sort_char(cs);
  return FALSE;
}


longlong my_strtoll10_8bit(CHARSET_INFO *cs __attribute__((unused)),
                           const char *nptr, char **endptr, int *error)
{
  return my_strtoll10(nptr, endptr, error);
}


int my_mb_ctype_8bit(CHARSET_INFO *cs, int *ctype,
                   const uchar *s, const uchar *e)
{
  if (s >= e)
  {
    *ctype= 0;
    return MY_CS_TOOSMALL;
  }
  *ctype= cs->m_ctype[*s + 1];
  return 1;
}


#define CUTOFF  (ULONGLONG_MAX / 10)
#define CUTLIM  (ULONGLONG_MAX % 10)
#define DIGITS_IN_ULONGLONG 20

static ulonglong d10[DIGITS_IN_ULONGLONG]=
{
  1,
  10,
  100,
  1000,
  10000,
  100000,
  1000000,
  10000000,
  100000000,
  1000000000,
  10000000000ULL,
  100000000000ULL,
  1000000000000ULL,
  10000000000000ULL,
  100000000000000ULL,
  1000000000000000ULL,
  10000000000000000ULL,
  100000000000000000ULL,
  1000000000000000000ULL,
  10000000000000000000ULL
};


/*

  Convert a string to unsigned long long integer value
  with rounding.
  
  SYNOPSIS
    my_strntoull10_8bit()
      cs              in      pointer to character set
      str             in      pointer to the string to be converted
      length          in      string length
      unsigned_flag   in      whether the number is unsigned
      endptr          out     pointer to the stop character
      error           out     returned error code

  DESCRIPTION
    This function takes the decimal representation of integer number
    from string str and converts it to an signed or unsigned
    long long integer value.
    Space characters and tab are ignored.
    A sign character might precede the digit characters.
    The number may have any number of pre-zero digits.
    The number may have decimal point and exponent.
    Rounding is always done in "away from zero" style:
      0.5  ->   1
     -0.5  ->  -1

    The function stops reading the string str after "length" bytes
    or at the first character that is not a part of correct number syntax:

    <signed numeric literal> ::=
      [ <sign> ] <exact numeric literal> [ E [ <sign> ] <unsigned integer> ]

    <exact numeric literal> ::=
                        <unsigned integer> [ <period> [ <unsigned integer> ] ]
                      | <period> <unsigned integer>
    <unsigned integer>   ::= <digit>...
     
  RETURN VALUES
    Value of string as a signed/unsigned longlong integer

    endptr cannot be NULL. The function will store the end pointer
    to the stop character here.

    The error parameter contains information how things went:
    0	     ok
    ERANGE   If the the value of the converted number is out of range
    In this case the return value is:
    - ULONGLONG_MAX if unsigned_flag and the number was too big
    - 0 if unsigned_flag and the number was negative
    - LONGLONG_MAX if no unsigned_flag and the number is too big
    - LONGLONG_MIN if no unsigned_flag and the number it too big negative
    
    EDOM If the string didn't contain any digits.
    In this case the return value is 0.
*/

ulonglong
my_strntoull10rnd_8bit(CHARSET_INFO *cs __attribute__((unused)),
                       const char *str, size_t length, int unsigned_flag,
                       char **endptr, int *error)
{
  const char *dot, *end9, *beg, *end= str + length;
  ulonglong ull;
  ulong ul;
  uchar ch;
  int shift= 0, digits= 0, negative, addon;

  /* Skip leading spaces and tabs */
  for ( ; str < end && my_isspace(&my_charset_latin1, *str) ; )
    str++;

  if (str >= end)
    goto ret_edom;

  if ((negative= (*str == '-')) || *str=='+') /* optional sign */
  {
    if (++str == end)
      goto ret_edom;
  }

  beg= str;
  end9= (str + 9) > end ? end : (str + 9);
  /* Accumulate small number into ulong, for performance purposes */
  for (ul= 0 ; str < end9 && (ch= (uchar) (*str - '0')) < 10; str++)
  {
    ul= ul * 10 + ch;
  }
  
  if (str >= end) /* Small number without dots and exponents */
  {
    *endptr= (char*) str;
    if (negative)
    {
      if (unsigned_flag)
      {
        *error= ul ? MY_ERRNO_ERANGE : 0;
        return 0;
      }
      else
      {
        *error= 0;
        return (ulonglong) (longlong) -(long) ul;
      }
    }
    else
    {
      *error=0;
      return (ulonglong) ul;
    }
  }
  
  digits= (int) (str - beg);

  /* Continue to accumulate into ulonglong */
  for (dot= NULL, ull= ul; str < end; str++)
  {
    if ((ch= (uchar) (*str - '0')) < 10)
    {
      if (ull < CUTOFF || (ull == CUTOFF && ch <= CUTLIM))
      {
        ull= ull * 10 + ch;
        digits++;
        continue;
      }
      /*
        Adding the next digit would overflow.
        Remember the next digit in "addon", for rounding.
        Scan all digits with an optional single dot.
      */
      if (ull == CUTOFF)
      {
        ull= ULONGLONG_MAX;
        addon= 1;
        str++;
      }
      else
        addon= (*str >= '5');
      if (!dot)
      {
        for ( ; str < end && (ch= (uchar) (*str - '0')) < 10; shift++, str++);
        if (str < end && *str == '.')
        {
          str++;
          for ( ; str < end && (ch= (uchar) (*str - '0')) < 10; str++);
        }
      }
      else
      {
        shift= (int) (dot - str);
        for ( ; str < end && (ch= (uchar) (*str - '0')) < 10; str++);
      }
      goto exp;
    }
    
    if (*str == '.')
    {
      if (dot)
      {
        /* The second dot character */
        addon= 0;
        goto exp;
      }
      else
      {
        dot= str + 1;
      }
      continue;
    }
    
    /* Unknown character, exit the loop */
    break; 
  }
  shift= dot ? (int)(dot - str) : 0; /* Right shift */
  addon= 0;

exp:    /* [ E [ <sign> ] <unsigned integer> ] */

  if (!digits)
  {
    str= beg;
    goto ret_edom;
  }
  
  if (str < end && (*str == 'e' || *str == 'E'))
  {
    str++;
    if (str < end)
    {
      int negative_exp, exponent;
      if ((negative_exp= (*str == '-')) || *str=='+')
      {
        if (++str == end)
        {
          str-= 2; /* 'e-' or 'e+' not followed by digits */
          goto ret_sign;
        }
      }
      if (shift > 0 && !negative_exp)
        goto ret_too_big;
      for (exponent= 0 ; str < end && (ch= (uchar) (*str - '0')) < 10; str++)
      {
        if (negative_exp)
        {
          if (exponent - shift > DIGITS_IN_ULONGLONG)
            goto ret_zero;
        }
        else
        {
          if (exponent + shift > DIGITS_IN_ULONGLONG)
            goto ret_too_big;
        }
        exponent= exponent * 10 + ch;
      }
      shift+= negative_exp ? -exponent : exponent;
    }
    else
      str--; /* 'e' not followed by digits */
  }
  
  if (shift == 0) /* No shift, check addon digit */
  {
    if (addon)
    {
      if (ull == ULONGLONG_MAX)
        goto ret_too_big;
      ull++;
    }
    goto ret_sign;
  }

  if (shift < 0) /* Right shift */
  {
    ulonglong d, r;
    
    if (-shift >= DIGITS_IN_ULONGLONG)
      goto ret_zero; /* Exponent is a big negative number, return 0 */
    
    d= d10[-shift];
    r= (ull % d) * 2;
    ull /= d;
    if (r >= d)
      ull++;
    goto ret_sign;
  }

  if (shift > DIGITS_IN_ULONGLONG) /* Huge left shift */
  {
    if (!ull)
      goto ret_sign;
    goto ret_too_big;
  }

  for ( ; shift > 0; shift--, ull*= 10) /* Left shift */
  {
    if (ull > CUTOFF)
      goto ret_too_big; /* Overflow, number too big */
  }

ret_sign:
  *endptr= (char*) str;

  if (!unsigned_flag)
  {
    if (negative)
    {
      if (ull >= (ulonglong) LONGLONG_MIN)
      {
        if (ull != (ulonglong) LONGLONG_MIN)
          *error= MY_ERRNO_ERANGE;
        return (ulonglong) LONGLONG_MIN;
      }
      *error= 0;
      return (ulonglong) -(longlong) ull;
    }
    else
    {
      if (ull > (ulonglong) LONGLONG_MAX)
      {
        *error= MY_ERRNO_ERANGE;
        return (ulonglong) LONGLONG_MAX;
      }
      *error= 0;
      return ull;
    }
  }

  /* Unsigned number */
  if (negative && ull)
  {
    *error= MY_ERRNO_ERANGE;
    return 0;
  }
  *error= 0;
  return ull;

ret_zero:
  *endptr= (char*) str;
  *error= 0;
  return 0;

ret_edom:
  *endptr= (char*) str;
  *error= MY_ERRNO_EDOM;
  return 0;
  
ret_too_big:
  *endptr= (char*) str;
  *error= MY_ERRNO_ERANGE;
  return unsigned_flag ?
         ULONGLONG_MAX :
         negative ? (ulonglong) LONGLONG_MIN : (ulonglong) LONGLONG_MAX;
}


/*
  Check if a constant can be propagated

  SYNOPSIS:
    my_propagate_simple()
    cs		Character set information
    str		String to convert to double
    length	Optional length for string.
    
  NOTES:
   Takes the string in the given charset and check
   if it can be safely propagated in the optimizer.
   
   create table t1 (
     s char(5) character set latin1 collate latin1_german2_ci);
   insert into t1 values (0xf6); -- o-umlaut
   select * from t1 where length(s)=1 and s='oe';

   The above query should return one row.
   We cannot convert this query into:
   select * from t1 where length('oe')=1 and s='oe';
   
   Currently we don't check the constant itself,
   and decide not to propagate a constant
   just if the collation itself allows tricky things
   like expansions and contractions. In the future
   we can write a more sophisticated functions to
   check the constants. For example, 'oa' can always
   be safety propagated in German2 because unlike 
   'oe' it does not have any special meaning.

  RETURN
    1 if constant can be safely propagated
    0 if it is not safe to propagate the constant
*/



my_bool my_propagate_simple(CHARSET_INFO *cs __attribute__((unused)),
                            const uchar *str __attribute__((unused)),
                            size_t length __attribute__((unused)))
{
  return 1;
}


my_bool my_propagate_complex(CHARSET_INFO *cs __attribute__((unused)),
                             const uchar *str __attribute__((unused)),
                             size_t length __attribute__((unused)))
{
  return 0;
}


void my_ci_set_strength(struct charset_info_st *cs, uint strength)
{
  DBUG_ASSERT(strength > 0);
  DBUG_ASSERT(strength <= MY_STRXFRM_NLEVELS);
  cs->levels_for_order= ((1 << strength) - 1);
}


void my_ci_set_level_flags(struct charset_info_st *cs, uint flags)
{
  DBUG_ASSERT(flags < (1<<MY_STRXFRM_NLEVELS));
  cs->levels_for_order= flags;
}

/*
  Normalize strxfrm flags

  SYNOPSIS:
    my_strxfrm_flag_normalize()
    cs       - the CHARSET_INFO pointer
    flags    - non-normalized flags
    
  NOTES:
    If levels are omitted, then 1-maximum is assumed.
    If any level number is greater than the maximum,
    it is treated as the maximum.

  RETURN
    normalized flags
*/

uint my_strxfrm_flag_normalize(CHARSET_INFO *cs, uint flags)
{
  uint maximum= my_bit_log2_uint32(cs->levels_for_order) + 1;
  DBUG_ASSERT(maximum >= 1 && maximum <= MY_STRXFRM_NLEVELS);
  
  /* If levels are omitted, then 1-maximum is assumed*/
  if (!(flags & MY_STRXFRM_LEVEL_ALL))
  {
    static uint def_level_flags[]= {0, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F };
    uint flag_pad= flags &
                   (MY_STRXFRM_PAD_WITH_SPACE | MY_STRXFRM_PAD_TO_MAXLEN);
    flags= def_level_flags[maximum] | flag_pad;
  }
  else
  {
    uint i;
    uint flag_lev= flags & MY_STRXFRM_LEVEL_ALL;
    uint flag_dsc= (flags >> MY_STRXFRM_DESC_SHIFT) & MY_STRXFRM_LEVEL_ALL;
    uint flag_rev= (flags >> MY_STRXFRM_REVERSE_SHIFT) & MY_STRXFRM_LEVEL_ALL;
    uint flag_pad= flags &
                   (MY_STRXFRM_PAD_WITH_SPACE | MY_STRXFRM_PAD_TO_MAXLEN);

    /*
      If any level number is greater than the maximum,
      it is treated as the maximum.
    */
    for (maximum--, flags= 0, i= 0; i < MY_STRXFRM_NLEVELS; i++)
    {
      uint src_bit= 1 << i;
      if (flag_lev & src_bit)
      {
        uint dst_bit= 1 << MY_MIN(i, maximum);
        flags|= dst_bit;
        flags|= (flag_dsc & dst_bit) << MY_STRXFRM_DESC_SHIFT;
        flags|= (flag_rev & dst_bit) << MY_STRXFRM_REVERSE_SHIFT;
      }
    }
    flags|= flag_pad;
  }
  
  return flags;
}


/*
  Apply DESC and REVERSE collation rules.

  SYNOPSIS:
    my_strxfrm_desc_and_reverse()
    str      - pointer to string
    strend   - end of string
    flags    - flags
    level    - which level, starting from 0.
    
  NOTES:
    Apply DESC or REVERSE or both flags.
    
    If DESC flag is given, then the weights
    come out NOTed or negated for that level.
    
    If REVERSE flags is given, then the weights come out in
    reverse order for that level, that is, starting with
    the last character and ending with the first character.
    
    If neither DESC nor REVERSE flags are give,
    the string is not changed.
    
*/
void
my_strxfrm_desc_and_reverse(uchar *str, uchar *strend,
                            uint flags, uint level)
{
  if (flags & (MY_STRXFRM_DESC_LEVEL1 << level))
  {
    if (flags & (MY_STRXFRM_REVERSE_LEVEL1 << level))
    {
      for (strend--; str <= strend;)
      {
        uchar tmp= *str;
        *str++= ~*strend;
        *strend--= ~tmp;
      }
    }
    else
    {
      for (; str < strend; str++)
        *str= ~*str;
    }
  }
  else if (flags & (MY_STRXFRM_REVERSE_LEVEL1 << level))
  {
    for (strend--; str < strend;)
    {
      uchar tmp= *str;
      *str++= *strend;
      *strend--= tmp;
    }
  }
}


my_strnxfrm_ret_t
my_strxfrm_pad_desc_and_reverse(CHARSET_INFO *cs,
                                uchar *str, uchar *frmend, uchar *strend,
                                uint nweights, uint flags, uint level)
{
  uint warnings= 0;
  if (nweights && (flags & MY_STRXFRM_PAD_WITH_SPACE))
  {
    uint fill_length_expected= nweights * cs->mbminlen; 
    uint fill_length= MY_MIN((uint) (strend - frmend), fill_length_expected);
    if (fill_length)
    {
      my_ci_fill(cs, (char*) frmend, fill_length, cs->pad_char);
      frmend+= fill_length;
    }
    if (fill_length < fill_length_expected)
      warnings= MY_STRNXFRM_TRUNCATED_WEIGHT_TRAILING_SPACE;
  }
  my_strxfrm_desc_and_reverse(str, frmend, flags, level);
  if ((flags & MY_STRXFRM_PAD_TO_MAXLEN) && frmend < strend)
  {
    size_t fill_length= strend - frmend;
    my_ci_fill(cs, (char*) frmend, fill_length, cs->pad_char);
    frmend= strend;
  }
  return my_strnxfrm_ret_construct(frmend - str, 0, warnings);
}


my_strnxfrm_ret_t
my_strxfrm_pad_desc_and_reverse_nopad(CHARSET_INFO *cs,
                                      uchar *str, uchar *frmend, uchar *strend,
                                      uint nweights, uint flags, uint level)
{
  uint warnings= 0;
  if (nweights && (flags & MY_STRXFRM_PAD_WITH_SPACE))
  {
    uint fill_length_expected= nweights * cs->mbminlen;
    uint fill_length= MY_MIN((uint) (strend - frmend), fill_length_expected);
    if (fill_length)
    {
      memset(frmend, 0x00, fill_length);
      frmend+= fill_length;
    }
    if (fill_length < fill_length_expected)
      warnings= MY_STRNXFRM_TRUNCATED_WEIGHT_TRAILING_SPACE;
  }
  my_strxfrm_desc_and_reverse(str, frmend, flags, level);
  if ((flags & MY_STRXFRM_PAD_TO_MAXLEN) && frmend < strend)
  {
    size_t fill_length= strend - frmend;
    memset(frmend, 0x00, fill_length);
    frmend= strend;
  }
  return my_strnxfrm_ret_construct(frmend - str, 0, warnings);
}


MY_CHARSET_HANDLER my_charset_8bit_handler=
{
    my_cset_init_8bit,
    my_numchars_8bit,
    my_charpos_8bit,
    my_lengthsp_8bit,
    my_numcells_8bit,
    my_mb_wc_8bit,
    my_wc_mb_8bit,
    my_mb_ctype_8bit,
    my_caseup_8bit,
    my_casedn_8bit,
    my_snprintf_8bit,
    my_long10_to_str_8bit,
    my_longlong10_to_str_8bit,
    my_fill_8bit,
    my_strntol_8bit,
    my_strntoul_8bit,
    my_strntoll_8bit,
    my_strntoull_8bit,
    my_strntod_8bit,
    my_strtoll10_8bit,
    my_strntoull10rnd_8bit,
    my_scan_8bit,
    my_charlen_8bit,
    my_well_formed_char_length_8bit,
    my_copy_8bit,
    my_wc_mb_bin, /* native_to_mb */
    my_wc_to_printable_8bit,
    my_casefold_multiply_1,
    my_casefold_multiply_1
};

MY_COLLATION_HANDLER my_collation_8bit_simple_ci_handler =
{
    my_coll_init_simple,	/* init */
    my_strnncoll_simple,
    my_strnncollsp_simple,
    my_strnncollsp_nchars_simple,
    my_strnxfrm_simple,
    my_strnxfrmlen_simple,
    my_like_range_simple,
    my_wildcmp_8bit,
    my_instr_simple,
    my_hash_sort_simple,
    my_propagate_simple,
    my_min_str_8bit_simple,
    my_max_str_8bit_simple,
    my_ci_get_id_generic,
    my_ci_get_collation_name_generic,
    my_ci_eq_collation_generic
};


MY_COLLATION_HANDLER my_collation_8bit_simple_nopad_ci_handler =
{
    my_coll_init_simple,	/* init */
    my_strnncoll_simple,
    my_strnncollsp_simple_nopad,
    my_strnncollsp_nchars_simple,
    my_strnxfrm_simple_nopad,
    my_strnxfrmlen_simple,
    my_like_range_simple,
    my_wildcmp_8bit,
    my_instr_simple,
    my_hash_sort_simple_nopad,
    my_propagate_simple,
    my_min_str_8bit_simple_nopad,
    my_max_str_8bit_simple,
    my_ci_get_id_generic,
    my_ci_get_collation_name_generic,
    my_ci_eq_collation_generic
};
