/**
 * @file
 *
 * A brief file description
 *
 * @section license License
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * add_text_transform.c: a plugin that adds a predefined comment to the beginning
 *                       of .m3u8 files and a comment with the number of lines
 *                       to the end of the file.
 */
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "add_text"

#define ASSERT_SUCCESS(_x) TSAssert((_x) == TS_SUCCESS)

struct MyData {
  TSVIO            output_vio;
  TSIOBuffer       output_buffer;
  TSIOBufferReader output_reader;
  bool             added_prepend;
  int              line_count;
  bool  header_added;
  bool footer_added;
};

static TSIOBuffer       comment_buffer;
static TSIOBufferReader comment_buffer_reader;
static int              comment_buffer_length;

static TSIOBuffer       footer_buffer;
static TSIOBufferReader footer_buffer_reader;

static char *prepend_text        = nullptr; // Prepend text from the config
static int   prepend_text_length = 0;

static MyData *
my_data_alloc()
{
  MyData *data = static_cast<MyData *>(TSmalloc(sizeof(MyData)));
  TSReleaseAssert(data);

  data->output_vio    = nullptr;
  data->output_buffer = nullptr;
  data->output_reader = nullptr;
  data->added_prepend = false;
  data->header_added = false;
  data->footer_added = false;
  data->line_count    = 0;

  return data;
}

static void
my_data_destroy(MyData *data)
{
  if (prepend_text) {
    free(prepend_text);
    prepend_text = NULL;
  }
  if (data) {
    if (data->output_buffer) {
      TSIOBufferDestroy(data->output_buffer);
    }
    TSfree(data);
  }
}

static void
handle_transform(TSCont contp)
{
  TSVConn output_conn;
  TSVIO   write_vio;
  MyData *data;
  int64_t towrite;

  /* Get the output connection where we'll write data to. */
  output_conn = TSTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer. If the
     private data structure pointer is nullptr, then we'll create it
     and initialize its internals. */
  data = static_cast<decltype(data)>(TSContDataGet(contp));
  if (!data) {
    towrite             = TSVIONBytesGet(write_vio);
    data                = my_data_alloc();
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    // data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, towrite);
    TSContDataSet(contp, data);
  }

  towrite = TSVIONTodoGet(write_vio);
  if (towrite > 0) {
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      // Count lines and copy the data

      if (!data->header_added) {
         prepend_text_length += 3;
         char header[prepend_text_length];
         header[prepend_text_length] = '\0';
        snprintf(header, sizeof(header), "##%s\n", prepend_text);
        TSIOBufferWrite(data->output_buffer, header, prepend_text_length);
        data->header_added = true;
      }

      TSIOBufferBlock blk = TSIOBufferReaderStart(TSVIOReaderGet(write_vio));
      while (blk) {
        int64_t     block_avail;
        const char *block_start = TSIOBufferBlockReadStart(blk, TSVIOReaderGet(write_vio), &block_avail);
        for (int64_t i = 0; i < block_avail; i++) {
          if (block_start[i] == '\n') {
            data->line_count++;
          }
        }
        printf("%s", block_start);
        TSIOBufferWrite(data->output_buffer, block_start, block_avail);
        blk = TSIOBufferBlockNext(blk);
      }
    
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
     
    }
     
      // if (data->output_vio) {
      //    TSVIOReenable(data->output_vio);
      // }
      if (TSVIONTodoGet(write_vio) > 0) {
           TSVIOReenable(write_vio);
      } else {
         if (!data->footer_added) {
         data->footer_added = true;
           char footer[256];
          snprintf(footer, sizeof(footer), "##Totals line:%d", data->line_count);
          TSIOBufferWrite(data->output_buffer, footer, strlen(footer));
          data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, towrite + strlen(footer) + prepend_text_length);
         }
      }
     
    // Send an event back to the upstream if required
    if (TSVIONTodoGet(write_vio) == 0 && data->footer_added) {
        TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
    }
     
  }
}

static int
add_text_transform(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  if (TSVConnClosedGet(contp)) {
    my_data_destroy(static_cast<MyData *>(TSContDataGet(contp)));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      TSVIO write_vio;
      write_vio = TSVConnWriteVIOGet(contp);
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_ERROR, write_vio);
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
    default:
      handle_transform(contp);
      break;
    }
  }

  return 0;
}

static int
transformable(TSHttpTxn txnp)
{
  int         url_len;
  const char *url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_len);

  if (url && (url_len >= 5) && (strncmp(url + url_len - 5, ".m3u8", 5) == 0)) {
    return 1;
  }
  return 0;
}

static void
transform_add(TSHttpTxn txnp)
{
  TSVConn connp = TSTransformCreate(add_text_transform, txnp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

static int
transform_plugin(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      transform_add(txnp);
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

  return 0;
}

static int
load_comment_in_file(const char *filename)
{
  FILE *file;
  long  file_size;
  // Open the file in read mode
  file = fopen(filename, "r");
  if (file == NULL) {
    TSError("[%s]Error opening file\n[%s]", PLUGIN_NAME, filename);
    return 0;
  }

  // Get the size of the file
  fseek(file, 0, SEEK_END); // Move to the end of the file
  file_size = ftell(file);  // Get the current position (size of the file)
  rewind(file);             // Move back to the beginning of the file

  // Allocate memory for the buffer
  prepend_text = (char *)malloc((file_size + 1) * sizeof(char));
  if (prepend_text == NULL) {
    perror("Memory allocation failed");
    fclose(file);
    return 0;
  }

  // Read the file content into the buffer
  fread(prepend_text, sizeof(char), file_size, file);
  prepend_text_length = file_size;
  // Clean up
  fclose(file);

  return 1;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Your Name";
  info.support_email = "your_email@example.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  if (argc != 2) {
    TSError("[%s] Usage: %s <prepend_text>", PLUGIN_NAME, argv[0]);
    return;
  }
  int loadRet = load_comment_in_file(argv[1]);
  if (!loadRet) {
    TSError("[%s] Failed to load comment in file", PLUGIN_NAME);
    prepend_text        = strdup(argv[1]);
    prepend_text_length = strlen(prepend_text);
  }

  TSError("[%s] CONGNV31 Load Comment done:", prepend_text);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, nullptr));
}
