// Some portions Copyright (c) 2004-2012 Sergey Lyubka
// Some portions Copyright (c) 2013 Cloudera Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef MONGOOSE_HEADER_INCLUDED
#define  MONGOOSE_HEADER_INCLUDED

#include <stdio.h>
#include <stddef.h>

// Define this when compiling to add shims for Mongoose APIs to the
// renamed squeasel APIs
#ifdef MONGOOSE_COMPATIBLE
#define mg_callbacks                sq_callbacks
#define mg_close_connection         sq_close_connection
#define mg_connection               sq_connection
#define mg_context                  sq_context
#define mg_download                 sq_download
#define mg_get_bound_addresses      sq_get_bound_addresses
#define mg_get_builtin_mime_type    sq_get_builtin_mime_type
#define mg_get_cookie               sq_get_cookie
#define mg_get_header               sq_get_header
#define mg_get_option               sq_get_option
#define mg_get_request_info         sq_get_request_info
#define mg_get_valid_option_names   sq_get_valid_option_names
#define mg_get_var                  sq_get_var
#define mg_header                   sq_header
#define mg_md5                      sq_md5
#define mg_modify_passwords_file    sq_modify_passwords_file
#define mg_printf                   sq_printf
#define mg_read                     sq_read
#define mg_request_info             sq_request_info
#define mg_send_file                sq_send_file
#define mg_start                    sq_start
#define mg_start_thread             sq_start_thread
#define mg_stop                     sq_stop
#define mg_thread_func_t            sq_thread_func_t
#define mg_upload                   sq_upload
#define mg_url_decode               sq_url_decode
#define mg_version                  sq_version
#define mg_websocket_write          sq_websocket_write
#define mg_write                    sq_write
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct sq_context;     // Handle for the HTTP service itself
struct sq_connection;  // Handle for the individual connection

struct sockaddr_in;

// This structure contains information about the HTTP request.
struct sq_request_info {
  const char *request_method; // "GET", "POST", etc
  const char *uri;            // URL-decoded URI
  const char *http_version;   // E.g. "1.0", "1.1"
  const char *query_string;   // URL part after '?', not including '?', or NULL
  const char *remote_user;    // Authenticated user, or NULL if no auth used
  long remote_ip;             // Client's IP address
  int remote_port;            // Client's port
  int is_ssl;                 // 1 if SSL-ed, 0 if not
  void *user_data;            // User data pointer passed to sq_start()
  void *conn_data;            // Connection-specific user data

  int num_headers;            // Number of HTTP headers
  struct sq_header {
    const char *name;         // HTTP header name
    const char *value;        // HTTP header value
  } http_headers[64];         // Maximum 64 headers
};

typedef enum sq_callback_result {
  // The callback didn't handle the request, and squeasel should
  // continue with request processing.
  SQ_CONTINUE_HANDLING = 0,
  // The callback handled the request, and the connection is still
  // in a valid state.
  SQ_HANDLED_OK = 1,
  // The callback handled the request, but no more requests should
  // be read from this connection (eg the request was invalid).
  SQ_HANDLED_CLOSE_CONNECTION = 2
} sq_callback_result_t;

// This structure needs to be passed to sq_start(), to let squeasel know
// which callbacks to invoke. For detailed description, see
// https://github.com/cloudera/squeasel/blob/master/UserManual.md
struct sq_callbacks {
  // Called when squeasel has received new HTTP request.
  // If callback returns one of the SQ_HANDLED_* results,
  // callback must process the request by sending valid HTTP headers and body,
  // and squeasel will not do any further processing.
  // If callback returns SQ_CONTINUE_HANDLING, squeasel processes the request itself.
  // In this case, callback must not send any data to the client.
  sq_callback_result_t (*begin_request)(struct sq_connection *);

  // Called when squeasel has finished processing request.
  void (*end_request)(const struct sq_connection *, int reply_status_code);

  // Called when squeasel is about to log a message. If callback returns
  // non-zero, squeasel does not log anything.
  int  (*log_message)(const struct sq_connection *, const char *message);

  // Called when squeasel initializes SSL library.
  int  (*init_ssl)(void *ssl_context, void *user_data);

  // Called when websocket request is received, before websocket handshake.
  // If callback returns 0, squeasel proceeds with handshake, otherwise
  // cinnection is closed immediately.
  int (*websocket_connect)(const struct sq_connection *);

  // Called when websocket handshake is successfully completed, and
  // connection is ready for data exchange.
  void (*websocket_ready)(struct sq_connection *);

  // Called when data frame has been received from the client.
  // Parameters:
  //    bits: first byte of the websocket frame, see websocket RFC at
  //          http://tools.ietf.org/html/rfc6455, section 5.2
  //    data, data_len: payload, with mask (if any) already applied.
  // Return value:
  //    non-0: keep this websocket connection opened.
  //    0:     close this websocket connection.
  int  (*websocket_data)(struct sq_connection *, int bits,
                         char *data, size_t data_len);

  // Called when squeasel tries to open a file. Used to intercept file open
  // calls, and serve file data from memory instead.
  // Parameters:
  //    path:     Full path to the file to open.
  //    data_len: Placeholder for the file size, if file is served from memory.
  // Return value:
  //    NULL: do not serve file from memory, proceed with normal file open.
  //    non-NULL: pointer to the file contents in memory. data_len must be
  //              initilized with the size of the memory block.
  const char * (*open_file)(const struct sq_connection *,
                             const char *path, size_t *data_len);

  // Called when squeasel is about to serve Lua server page (.lp file), if
  // Lua support is enabled.
  // Parameters:
  //   lua_context: "lua_State *" pointer.
  void (*init_lua)(struct sq_connection *, void *lua_context);

  // Called when squeasel has uploaded a file to a temporary directory as a
  // result of sq_upload() call.
  // Parameters:
  //    file_file: full path name to the uploaded file.
  void (*upload)(struct sq_connection *, const char *file_name);

  // Called when squeasel is about to send HTTP error to the client.
  // Implementing this callback allows to create custom error pages.
  // Parameters:
  //   status: HTTP error status code.
  int  (*http_error)(struct sq_connection *, int status);

  // Called on a worker thread when it starts.
  void (*enter_worker_thread)();

  // Called on a worker thread when it ends.
  void (*leave_worker_thread)();
};

// Start web server.
//
// Parameters:
//   callbacks: sq_callbacks structure with user-defined callbacks.
//   options: NULL terminated list of option_name, option_value pairs that
//            specify Squeasel configuration parameters.
//
// Side-effects: on UNIX, ignores SIGCHLD and SIGPIPE signals. If custom
//    processing is required for these, signal handlers must be set up
//    after calling sq_start().
//
//
// Example:
//   const char *options[] = {
//     "document_root", "/var/www",
//     "listening_ports", "80,443s",
//     NULL
//   };
//   struct sq_context *ctx = sq_start(&my_func, NULL, options);
//
// Refer to https://github.com/cloudera/squeasel/blob/master/UserManual.md
// for the list of valid option and their possible values.
//
// Return:
//   web server context, or NULL on error.
struct sq_context *sq_start(const struct sq_callbacks *callbacks,
                            void *user_data,
                            const char **configuration_options);


// Stop the web server.
//
// Must be called last, when an application wants to stop the web server and
// release all associated resources. This function blocks until all Squeasel
// threads are stopped. Context pointer becomes invalid.
void sq_stop(struct sq_context *);


// Get the value of particular configuration parameter.
// The value returned is read-only. Squeasel does not allow changing
// configuration at run time.
// If given parameter name is not valid, NULL is returned. For valid
// names, return value is guaranteed to be non-NULL. If parameter is not
// set, zero-length string is returned.
const char *sq_get_option(const struct sq_context *ctx, const char *name);


// Return array of strings that represent valid configuration options.
// For each option, option name and default value is returned, i.e. the
// number of entries in the array equals to number_of_options x 2.
// Array is NULL terminated.
const char **sq_get_valid_option_names(void);


// Return the addresses that the given context is bound to. *addrs is allocated
// using malloc to be an array of sockaddr*, each of which is itself malloced.
// *num_addrs is set to the number of returned addresses.
// The user is responsible for calling free() on each address as well as the
// 'addrs' array itself, unless an error occurs.
//
// Returns 0 on success, non-zero if an error occurred.
int sq_get_bound_addresses(const struct sq_context *ctx, struct sockaddr_in ***addrs,
                           int *num_addrs);

// Add, edit or delete the entry in the passwords file.
//
// This function allows an application to manipulate .htpasswd files on the
// fly by adding, deleting and changing user records. This is one of the
// several ways of implementing authentication on the server side. For another,
// cookie-based way please refer to the examples/chat.c in the source tree.
//
// If password is not NULL, entry is added (or modified if already exists).
// If password is NULL, entry is deleted.
//
// Return:
//   1 on success, 0 on error.
int sq_modify_passwords_file(const char *passwords_file_name,
                             const char *domain,
                             const char *user,
                             const char *password);


// Return information associated with the request.
struct sq_request_info *sq_get_request_info(struct sq_connection *);


// Send data to the client.
// Return:
//  0   when the connection has been closed
//  -1  on error
//  >0  number of bytes written on success
int sq_write(struct sq_connection *, const void *buf, size_t len);


// Send data to a websocket client wrapped in a websocket frame.
// It is unsafe to read/write to this connection from another thread.
// This function is available when squeasel is compiled with -DUSE_WEBSOCKET
//
// Return:
//  0   when the connection has been closed
//  -1  on error
//  >0  number of bytes written on success
int sq_websocket_write(struct sq_connection* conn, int opcode,
                       const char *data, size_t data_len);

// Opcodes, from http://tools.ietf.org/html/rfc6455
enum {
  WEBSOCKET_OPCODE_CONTINUATION = 0x0,
  WEBSOCKET_OPCODE_TEXT = 0x1,
  WEBSOCKET_OPCODE_BINARY = 0x2,
  WEBSOCKET_OPCODE_CONNECTION_CLOSE = 0x8,
  WEBSOCKET_OPCODE_PING = 0x9,
  WEBSOCKET_OPCODE_PONG = 0xa
};


// Macros for enabling compiler-specific checks for printf-like arguments.
#undef PRINTF_FORMAT_STRING
#if defined(_MSC_VER) && _MSC_VER >= 1400
#include <sal.h>
#if defined(_MSC_VER) && _MSC_VER > 1400
#define PRINTF_FORMAT_STRING(s) _Printf_format_string_ s
#else
#define PRINTF_FORMAT_STRING(s) __format_string s
#endif
#else
#define PRINTF_FORMAT_STRING(s) s
#endif

#ifdef __GNUC__
#define PRINTF_ARGS(x, y) __attribute__((format(printf, x, y)))
#else
#define PRINTF_ARGS(x, y)
#endif

// Send data to the client using printf() semantics.
//
// Works exactly like sq_write(), but allows to do message formatting.
int sq_printf(struct sq_connection *,
              PRINTF_FORMAT_STRING(const char *fmt), ...) PRINTF_ARGS(2, 3);


// Send contents of the entire file together with HTTP headers.
void sq_send_file(struct sq_connection *conn, const char *path);


// Read data from the remote end, return number of bytes read.
// Return:
//   0     connection has been closed by peer. No more data could be read.
//   < 0   read error. No more data could be read from the connection.
//   > 0   number of bytes read into the buffer.
int sq_read(struct sq_connection *, void *buf, size_t len);


// Get the value of particular HTTP header.
//
// This is a helper function. It traverses request_info->http_headers array,
// and if the header is present in the array, returns its value. If it is
// not present, NULL is returned.
const char *sq_get_header(const struct sq_connection *, const char *name);


// Get a value of particular form variable.
//
// Parameters:
//   data: pointer to form-uri-encoded buffer. This could be either POST data,
//         or request_info.query_string.
//   data_len: length of the encoded data.
//   var_name: variable name to decode from the buffer
//   dst: destination buffer for the decoded variable
//   dst_len: length of the destination buffer
//
// Return:
//   On success, length of the decoded variable.
//   On error:
//      -1 (variable not found).
//      -2 (destination buffer is NULL, zero length or too small to hold the
//          decoded variable).
//
// Destination buffer is guaranteed to be '\0' - terminated if it is not
// NULL or zero length.
int sq_get_var(const char *data, size_t data_len,
               const char *var_name, char *dst, size_t dst_len);

// Fetch value of certain cookie variable into the destination buffer.
//
// Destination buffer is guaranteed to be '\0' - terminated. In case of
// failure, dst[0] == '\0'. Note that RFC allows many occurrences of the same
// parameter. This function returns only first occurrence.
//
// Return:
//   On success, value length.
//   On error:
//      -1 (either "Cookie:" header is not present at all or the requested
//          parameter is not found).
//      -2 (destination buffer is NULL, zero length or too small to hold the
//          value).
int sq_get_cookie(const char *cookie, const char *var_name,
                  char *buf, size_t buf_len);


// Download data from the remote web server.
//   host: host name to connect to, e.g. "foo.com", or "10.12.40.1".
//   port: port number, e.g. 80.
//   use_ssl: wether to use SSL connection.
//   error_buffer, error_buffer_size: error message placeholder.
//   request_fmt,...: HTTP request.
// Return:
//   On success, valid pointer to the new connection, suitable for sq_read().
//   On error, NULL. error_buffer contains error message.
// Example:
//   char ebuf[100];
//   struct sq_connection *conn;
//   conn = sq_download("google.com", 80, 0, ebuf, sizeof(ebuf),
//                      "%s", "GET / HTTP/1.0\r\nHost: google.com\r\n\r\n");
struct sq_connection *sq_download(const char *host, int port, int use_ssl,
                                  char *error_buffer, size_t error_buffer_size,
                                  PRINTF_FORMAT_STRING(const char *request_fmt),
                                  ...) PRINTF_ARGS(6, 7);


// Close the connection opened by sq_download().
void sq_close_connection(struct sq_connection *conn);


// File upload functionality. Each uploaded file gets saved into a temporary
// file and SQ_UPLOAD event is sent.
// Return number of uploaded files.
int sq_upload(struct sq_connection *conn, const char *destination_dir);


// Convenience function -- create detached thread.
// Return: 0 on success, non-0 on error.
typedef void * (*sq_thread_func_t)(void *);
int sq_start_thread(sq_thread_func_t f, void *p);


// Return builtin mime type for the given file name.
// For unrecognized extensions, "text/plain" is returned.
const char *sq_get_builtin_mime_type(const char *file_name);


// Return Squeasel version.
const char *sq_version(void);

// URL-decode input buffer into destination buffer.
// 0-terminate the destination buffer.
// form-url-encoded data differs from URI encoding in a way that it
// uses '+' as character for space, see RFC 1866 section 8.2.1
// http://ftp.ics.uci.edu/pub/ietf/html/rfc1866.txt
// Return: length of the decoded data, or -1 if dst buffer is too small.
int sq_url_decode(const char *src, int src_len, char *dst,
                  int dst_len, int is_form_url_encoded);

// MD5 hash given strings.
// Buffer 'buf' must be 33 bytes long. Varargs is a NULL terminated list of
// ASCIIz strings. When function returns, buf will contain human-readable
// MD5 hash. Example:
//   char buf[33];
//   sq_md5(buf, "aa", "bb", NULL);
char *sq_md5(char buf[33], ...);


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // MONGOOSE_HEADER_INCLUDED
