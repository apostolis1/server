/* Copyright (c) 2010, 2023, Oracle and/or its affiliates.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

/**
  @file storage/perfschema/table_ews_by_thread_by_event_name.cc
  Table EVENTS_WAITS_SUMMARY_BY_HOST_BY_EVENT_NAME (implementation).
*/

#include "my_global.h"
#include "my_thread.h"
#include "pfs_instr_class.h"
#include "pfs_column_types.h"
#include "pfs_column_values.h"
#include "table_ews_by_thread_by_event_name.h"
#include "pfs_global.h"
#include "pfs_visitor.h"
#include "pfs_buffer_container.h"
#include "field.h"

THR_LOCK table_ews_by_thread_by_event_name::m_table_lock;

PFS_engine_table_share_state
table_ews_by_thread_by_event_name::m_share_state = {
  false /* m_checked */
};

PFS_engine_table_share
table_ews_by_thread_by_event_name::m_share=
{
  { C_STRING_WITH_LEN("events_waits_summary_by_thread_by_event_name") },
  &pfs_truncatable_acl,
  table_ews_by_thread_by_event_name::create,
  NULL, /* write_row */
  table_ews_by_thread_by_event_name::delete_all_rows,
  table_ews_by_thread_by_event_name::get_row_count,
  sizeof(pos_ews_by_thread_by_event_name),
  &m_table_lock,
  { C_STRING_WITH_LEN("CREATE TABLE events_waits_summary_by_thread_by_event_name("
                      "THREAD_ID BIGINT unsigned not null comment 'Thread associated with the event. Together with EVENT_NAME uniquely identifies the row.',"
                      "EVENT_NAME VARCHAR(128) not null comment 'Event name. Used together with THREAD_ID for grouping events.',"
                      "COUNT_STAR BIGINT unsigned not null comment 'Number of summarized events',"
                      "SUM_TIMER_WAIT BIGINT unsigned not null comment 'Total wait time of the summarized events that are timed.',"
                      "MIN_TIMER_WAIT BIGINT unsigned not null comment 'Minimum wait time of the summarized events that are timed.',"
                      "AVG_TIMER_WAIT BIGINT unsigned not null comment 'Average wait time of the summarized events that are timed.',"
                      "MAX_TIMER_WAIT BIGINT unsigned not null comment 'Maximum wait time of the summarized events that are timed.')") },
  false, /* m_perpetual */
  false, /* m_optional */
  &m_share_state
};

PFS_engine_table*
table_ews_by_thread_by_event_name::create(void)
{
  return new table_ews_by_thread_by_event_name();
}

int
table_ews_by_thread_by_event_name::delete_all_rows(void)
{
  reset_events_waits_by_thread();
  return 0;
}

ha_rows
table_ews_by_thread_by_event_name::get_row_count(void)
{
  return global_thread_container.get_row_count() * wait_class_max;
}

table_ews_by_thread_by_event_name::table_ews_by_thread_by_event_name()
  : PFS_engine_table(&m_share, &m_pos),
    m_row_exists(false), m_pos(), m_next_pos()
{
  // For all cases except IDLE
  m_normalizer = time_normalizer::get_wait();
}

void table_ews_by_thread_by_event_name::reset_position(void)
{
  m_pos.reset();
  m_next_pos.reset();
}

int table_ews_by_thread_by_event_name::rnd_next(void)
{
  PFS_thread *thread;
  PFS_instr_class *instr_class;
  bool has_more_thread= true;

  for (m_pos.set_at(&m_next_pos);
       has_more_thread;
       m_pos.next_thread())
  {
    thread= global_thread_container.get(m_pos.m_index_1, & has_more_thread);
    if (thread != NULL)
    {
      for ( ;
           m_pos.has_more_view();
           m_pos.next_view())
      {
        switch (m_pos.m_index_2)
        {
        case pos_ews_by_thread_by_event_name::VIEW_MUTEX:
          instr_class= find_mutex_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_RWLOCK:
          instr_class= find_rwlock_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_COND:
          instr_class= find_cond_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_FILE:
          instr_class= find_file_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_TABLE:
          instr_class= find_table_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_SOCKET:
          instr_class= find_socket_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_IDLE:
          instr_class= find_idle_class(m_pos.m_index_3);
          break;
        case pos_ews_by_thread_by_event_name::VIEW_METADATA:
          instr_class= find_metadata_class(m_pos.m_index_3);
          break;
        default:
          assert(false);
          instr_class= NULL;
          break;
        }

        if (instr_class != NULL)
        {
          make_row(thread, instr_class);
          m_next_pos.set_after(&m_pos);
          return 0;
        }
      }
    }
  }

  return HA_ERR_END_OF_FILE;
}

int
table_ews_by_thread_by_event_name::rnd_pos(const void *pos)
{
  PFS_thread *thread;
  PFS_instr_class *instr_class;

  set_position(pos);

  thread= global_thread_container.get(m_pos.m_index_1);
  if (thread != NULL)
  {
    switch (m_pos.m_index_2)
    {
    case pos_ews_by_thread_by_event_name::VIEW_MUTEX:
      instr_class= find_mutex_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_RWLOCK:
      instr_class= find_rwlock_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_COND:
      instr_class= find_cond_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_FILE:
      instr_class= find_file_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_TABLE:
      instr_class= find_table_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_SOCKET:
      instr_class= find_socket_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_IDLE:
      instr_class= find_idle_class(m_pos.m_index_3);
      break;
    case pos_ews_by_thread_by_event_name::VIEW_METADATA:
      instr_class= find_metadata_class(m_pos.m_index_3);
      break;
    default:
      assert(false);
      instr_class= NULL;
    }

    if (instr_class)
    {
      make_row(thread, instr_class);
      return 0;
    }
  }

  return HA_ERR_RECORD_DELETED;
}

void table_ews_by_thread_by_event_name
::make_row(PFS_thread *thread, PFS_instr_class *klass)
{
  time_normalizer *normalizer = m_normalizer;
  pfs_optimistic_state lock;
  m_row_exists= false;

  /* Protect this reader against a thread termination */
  thread->m_lock.begin_optimistic_lock(&lock);

  m_row.m_thread_internal_id= thread->m_thread_internal_id;

  m_row.m_event_name.make_row(klass);

  PFS_connection_wait_visitor visitor(klass);
  PFS_connection_iterator::visit_thread(thread, &visitor);

  /*
    If the aggregation for this class is deferred, then we must pull the
    current wait stats from the instances associated with this thread.
  */
  if (klass->is_deferred())
  {
    /* Visit instances owned by this thread. Do not visit the class. */
    PFS_instance_wait_visitor inst_visitor;
    PFS_instance_iterator::visit_instances(klass, &inst_visitor,
                                           thread, false);
    /* Combine the deferred stats and global stats */
    visitor.m_stat.aggregate(&inst_visitor.m_stat);
  }

  if (! thread->m_lock.end_optimistic_lock(&lock))
    return;

  m_row_exists= true;

  if (klass->m_type == PFS_CLASS_IDLE)
  {
    normalizer = time_normalizer::get_idle();
  }

  m_row.m_stat.set(normalizer, &visitor.m_stat);
}

int table_ews_by_thread_by_event_name
::read_row_values(TABLE *table, unsigned char *, Field **fields,
                  bool read_all)
{
  Field *f;

  if (unlikely(! m_row_exists))
    return HA_ERR_RECORD_DELETED;

  /* Set the null bits */
  assert(table->s->null_bytes == 0);

  for (; (f= *fields) ; fields++)
  {
    if (read_all || bitmap_is_set(table->read_set, f->field_index))
    {
      switch(f->field_index)
      {
      case 0: /* THREAD_ID */
        set_field_ulonglong(f, m_row.m_thread_internal_id);
        break;
      case 1: /* EVENT_NAME */
        m_row.m_event_name.set_field(f);
        break;
      default: /* 2, ... COUNT/SUM/MIN/AVG/MAX */
        m_row.m_stat.set_field(f->field_index - 2, f);
        break;
      }
    }
  }

  return 0;
}

