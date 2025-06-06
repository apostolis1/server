/* Copyright (C) 2008 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA */

#include "../maria_def.h"
#include "sequence_storage.h"


/**
  @brief Initializes the sequence from the sequence file.

  @param seq             Reference on the sequence storage.
  @param file            Path to the file where to write the sequence

  @retval 0 OK
  @retval 1 Error
*/

my_bool seq_storage_reader_init(SEQ_STORAGE *seq, const char *file)
{
  FILE *fd;
  seq->pos= 0;
  if ((fd= my_fopen(file, O_RDONLY, MYF(MY_WME))) == NULL)
    return 1;
  if (my_init_dynamic_array(PSI_NOT_INSTRUMENTED, &seq->seq, sizeof(ulong), 10,
                            10, MYF(0)))
    return 1;

  for(;;)
  {
    ulong num;
    char line[22];
    if (fgets(line, sizeof(line), fd) == NULL)
      break;
    num= atol(line);
    if (insert_dynamic(&seq->seq, (uchar*) &num))
      return 1;
  }
  fclose(fd);
  return 0;
}


/**
  @brief Gets next number from the sequence storage

  @param seq             Reference on the sequence storage.

  @return Next number from the sequence.
*/

ulong seq_storage_next(SEQ_STORAGE *seq)
{
  DBUG_ASSERT(seq->seq.elements > 0);
  DBUG_ASSERT(seq->pos < seq->seq.elements);
  return (*(dynamic_element(&seq->seq, seq->pos++, ulong *)));
}


/**
  @brief Frees resources allocated for the storage

  @param seq             Reference on the sequence storage.
*/

void seq_storage_destroy(SEQ_STORAGE *seq)
{
  delete_dynamic(&seq->seq);
}


/**
  @brief Starts the sequence from beginning

  @param seq             Reference on the sequence storage.
*/

void seq_storage_rewind(SEQ_STORAGE *seq)
{
  seq->pos= 0;
}

/**
  @brief Writes a number to the sequence file.

  @param file            Path to the file where to write the sequence
  @param num             Number to be written

  @retval 0 OK
  @retval 1 Error
*/

my_bool seq_storage_write(const char *file, ulong num)
{
  FILE *fd;
  return  ((fd= my_fopen(file, O_CREAT | O_APPEND | O_WRONLY, MYF(MY_WME))) ==
           NULL ||
           fprintf(fd, "%lu\n", num) < 0 ||
           fclose(fd) != 0);
}
