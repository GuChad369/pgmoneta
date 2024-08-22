/*
 * Copyright (C) 2024 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <extension.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdlib.h>

static int query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr);

int
pgmoneta_ext_is_installed(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT * FROM pg_available_extensions WHERE name = 'pgmoneta_ext';", qr);
}

int
pgmoneta_ext_version(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_version();", qr);
}

int
pgmoneta_ext_switch_wal(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_switch_wal();", qr);
}

int
pgmoneta_ext_checkpoint(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_checkpoint();", qr);
}

int
pgmoneta_ext_priviledge(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT rolsuper FROM pg_roles WHERE rolname = current_user;", qr);
}

int
pgmoneta_ext_get_file(SSL* ssl, int socket, const char* file_path, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_get_file('%s');", file_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_get_files(SSL* ssl, int socket, const char* file_path, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_get_files('%s');", file_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_create_manifest_table(SSL* ssl, int socket, const char* file_path, struct query_response** qr)
{
   char line[128];
   char insert_sql[2048];
   int result = 1;
   FILE* file;

   char* create_table_sql = "CREATE TABLE IF NOT EXISTS backup_manifest ("
                            "filename TEXT, "
                            "checksum TEXT);";

   result = query_execute(ssl, socket, create_table_sql, qr);
   if (result != 0)
   {
      pgmoneta_log_error("Failed to create backup_manifest table");
      return result;
   }

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      pgmoneta_log_error("Failed to open backup.manifest file: %s", file_path);
      return 1;
   }

   while (fgets(line, sizeof(line), file))
   {
      char* filename = strtok(line, ",");
      char* checksum = strtok(NULL, ",");

      if (filename && checksum)
      {
         checksum[strcspn(checksum, "\r\n")] = 0;

         // prevent SQL injection
         char escaped_filename[512];
         char escaped_checksum[512];

         char* ef_ptr = escaped_filename;
         for (const char* f_ptr = filename; *f_ptr; f_ptr++)
         {
            if (*f_ptr == '\'')
            {
               *ef_ptr++ = '\'';
            }
            *ef_ptr++ = *f_ptr;
         }
         *ef_ptr = '\0';

         char* ec_ptr = escaped_checksum;
         for (const char* c_ptr = checksum; *c_ptr; c_ptr++)
         {
            if (*c_ptr == '\'')
            {
               *ec_ptr++ = '\'';
            }
            *ec_ptr++ = *c_ptr;
         }
         *ec_ptr = '\0';

         snprintf(insert_sql, sizeof(insert_sql),
                  "INSERT INTO backup_manifest (filename, checksum) VALUES ('%s', '%s');",
                  escaped_filename, escaped_checksum);

         result = query_execute(ssl, socket, insert_sql, qr);
         if (result != 0)
         {
            pgmoneta_log_error("Failed to insert into backup_manifest: %s, %s", filename, checksum);
            fclose(file);
            return result;
         }
      }
   }

   fclose(file);
   return 0;
}

int
pgmoneta_ext_delete_manifest_table(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "DROP TABLE backup_manifest;", qr);
}

static int
query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr)
{
   struct message* query_msg = NULL;

   if (pgmoneta_create_query_message(qs, &query_msg) != MESSAGE_STATUS_OK || query_msg == NULL)
   {
      pgmoneta_log_info("Failed to create query message");
      goto error;
   }

   if (pgmoneta_query_execute(ssl, socket, query_msg, qr) != 0 || qr == NULL)
   {
      pgmoneta_log_info("Failed to execute query");
      goto error;
   }

   pgmoneta_free_message(query_msg);

   return 0;

error:
   if (query_msg != NULL)
   {
      pgmoneta_free_message(query_msg);
   }

   return 1;
}
