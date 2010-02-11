/*-
 * Copyright (c) 2009 Alexey Illarionov <littlesavage@rambler.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/errno.h>
#include <sys/types.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ourfa.h"

#define DEFAULT_LOGIN "init"
#define DEFAULT_PASS "init"
#define DEFAULT_SERVERPORT "localhost"
#define DEFAULT_PORT 11758
#define DEFAULT_TIMEOUT 5
#define DEFAULT_LOGIN_TYPE OURFA_LOGIN_USER

struct ourfa_t {
   unsigned proto;
   unsigned   login_type;
   unsigned   ssl;
   unsigned   timeout;
   char *login;
   char *pass;
   char *server_port;

   ourfa_xmlapi_t *xmlapi;
   ourfa_conn_t *conn;

   char	 err_msg[500];
   FILE	 *debug_stream;
};

enum dump_format_t {
   DUMP_FORMAT_XML,
   DUMP_FORMAT_BATCH
};

/* From ourfa_xmlapi_dump.c */
int ourfa_xmlapi_xml_dump(ourfa_xmlapi_t *api,
      const char *func_name,
      ourfa_hash_t *h, FILE *stream, unsigned is_input);
int ourfa_xmlapi_batch_dump(ourfa_xmlapi_t *api,
      const char *func_name,
      ourfa_hash_t *h, FILE *stream, unsigned is_input);

static int set_err(ourfa_t *ourfa, const char *fmt, ...);

const char *ourfa_last_err_str(ourfa_t *ourfa)
{
   if (ourfa == NULL)
      return NULL;
   return ourfa->err_msg;
}

static int set_err(ourfa_t *ourfa, const char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(ourfa->err_msg, sizeof(ourfa->err_msg), fmt, ap);
   va_end(ap);

   return -1;
}

ourfa_t *ourfa_new()
{
   ourfa_t *res;

   res = (ourfa_t *)malloc(sizeof(struct ourfa_t)+1);

   if (res == NULL)
      return NULL;

   res->proto = OURFA_PROTO_VERSION;
   res->login = NULL;
   res->pass = NULL;
   res->server_port = NULL;
   res->login_type = DEFAULT_LOGIN_TYPE;
   res->ssl = 0;
   res->xmlapi = NULL;
   res->conn = NULL;
   res->err_msg[0] = '\0';
   res->timeout = DEFAULT_TIMEOUT;
   res->debug_stream = NULL;

   return res;
}

void ourfa_free(ourfa_t *ourfa)
{
   if (ourfa == NULL)
      return;
   free(ourfa->login);
   free(ourfa->pass);
   free(ourfa->server_port);

   ourfa_conn_close(ourfa->conn);
   ourfa_xmlapi_free(ourfa->xmlapi);

   free(ourfa);

   return;
}

int ourfa_set_debug_stream(ourfa_t *ourfa, FILE *stream)
{
   if (ourfa == NULL)
      return -1;
   ourfa->debug_stream = stream;

   if (ourfa->conn) {
      ourfa_conn_set_debug_stream(ourfa->conn, stream);
   }

   return 0;
}

int ourfa_set_conf(
      ourfa_t    *ctx,
      const char *login,
      const char *pass,
      const char *server_port,
      unsigned   *login_type,
      unsigned   *ssl,
      const char *api_xml_dir,
      const char *api_xml_file,
      int        *timeout
      )
{
   ourfa_t tmp;

   if (ctx == NULL)
      return -1;

   if (ctx->conn)
      return set_err(ctx, "Can not set configuration when online.  Disconnect first");

   tmp.login = tmp.pass = tmp.server_port = NULL;
   tmp.xmlapi = NULL;
   ctx->err_msg[0] = '\0';

   /* check new parameters */
   if (login != NULL) {
      tmp.login = strdup(login);
      if (tmp.login == NULL) {
	 set_err(ctx, "Malloc failed (%s) %s", "login", strerror(errno));
	 goto setconf_error;
      }
   }
   if (pass != NULL) {
      tmp.pass = strdup(pass);
      if (tmp.pass == NULL) {
	 set_err(ctx, "Malloc failed (%s) %s", "pass", strerror(errno));
	 goto setconf_error;
      }
   }
   if (server_port != NULL) {
      tmp.server_port = strdup(server_port);
      if (tmp.server_port == NULL) {
	 set_err(ctx, "Malloc failed (%s) %s", "server_port", strerror(errno));
	 goto setconf_error;
      }
   }

   if (login_type != NULL) {
      tmp.login_type = *login_type;
      if(!ourfa_is_valid_login_type(tmp.login_type)) {
	 set_err(ctx, "Invalid login type %i", tmp.login_type);
	 goto setconf_error;
      }
   }

   if (ssl != NULL)
      tmp.ssl = *ssl;

   tmp.xmlapi = ourfa_xmlapi_new(api_xml_dir, api_xml_file, ctx->err_msg, sizeof(ctx->err_msg));
   if (tmp.xmlapi == NULL)
      goto setconf_error;

   if (timeout != NULL)
      tmp.timeout = *timeout;

   /* set new parameters */
   if (login != NULL) {
      free(ctx->login);
      ctx->login = tmp.login;
   }
   if (pass != NULL) {
      free(ctx->pass);
      ctx->pass = tmp.pass;
   }
   if (server_port != NULL) {
      free(ctx->server_port);
      ctx->server_port = tmp.server_port;
   }

   if (login_type != NULL) {
      ctx->login_type = *login_type;
   }

   if (ssl != NULL) {
      ctx->ssl = *ssl;
   }

   ourfa_xmlapi_free(ctx->xmlapi);
   ctx->xmlapi = tmp.xmlapi;

   if (timeout != NULL) {
      ctx->timeout = *timeout;
   }

   return 0;

setconf_error:
   ourfa_xmlapi_free(tmp.xmlapi);
   free(tmp.server_port);
   free(tmp.pass);
   free(tmp.login);
   return -1;
}


int ourfa_connect(ourfa_t *ourfa)
{
   if (ourfa == NULL)
      return -1;

   ourfa->err_msg[0] = '\0';
   ourfa->conn = ourfa_conn_open(
	 ourfa->server_port ? ourfa->server_port : DEFAULT_SERVERPORT,
	 ourfa->login ? ourfa->login : DEFAULT_LOGIN,
	 ourfa->pass ? ourfa->pass : DEFAULT_PASS,
	 ourfa->login_type,
	 ourfa->timeout,
	 ourfa->ssl,
	 ourfa->err_msg,
	 sizeof(ourfa->err_msg)
	 );

   if (ourfa->conn == NULL) {
      return -1;
   }

   return 0;
}

int ourfa_disconnect(ourfa_t *ourfa)
{
   if (!ourfa)
      return -1;

   ourfa_conn_close(ourfa->conn);
   ourfa->conn = NULL;

   return 0;
}

ssize_t ourfa_send_packet(ourfa_t *ourfa, const ourfa_pkt_t *pkt)
{
   ssize_t res;

   if (!ourfa || !ourfa->conn)
      return -1;

   res = ourfa_conn_send_packet(ourfa->conn, pkt);

   if (res < 0) {
      set_err(ourfa, "%s", ourfa_conn_last_err_str(ourfa->conn));
   }

   return res;
}


ssize_t ourfa_recv_packet(ourfa_t *ourfa, ourfa_pkt_t **res)
{
   ssize_t res0;

   if (!ourfa || !ourfa->conn)
      return -1;

   res0 = ourfa_conn_recv_packet(ourfa->conn, res);

   if (res0 < 0) {
      set_err(ourfa, "%s", ourfa_conn_last_err_str(ourfa->conn));
   }

   return res0;
}

int ourfa_call(ourfa_t *ourfa, const char *func,
      ourfa_hash_t *in,
      ourfa_hash_t **out)
{
   ourfa_xmlapictx_t *ctx;
   ourfa_pkt_t *pkt_in, *pkt_out;
   ourfa_hash_t *res_h;
   void *resp_ctx;
   const ourfa_attr_hdr_t *attr_list;
   int last_err;
   ssize_t recvd_bytes;

   if ((ourfa == NULL) || (func == NULL))
      return -1;

   ourfa->err_msg[0]='\0';

   if (ourfa->xmlapi == NULL)
      return set_err(ourfa, "XML api not loaded");

   ctx = ourfa_xmlapictx_new(ourfa->xmlapi, func, 0, NULL, NULL, 0, NULL);
   if (ctx == NULL)
      return set_err(ourfa, "%s",
	    ourfa_xmlapi_last_err_str(ourfa->xmlapi));


   pkt_in = NULL;
   if (ourfa_xmlapictx_have_input_parameters(ctx)) {
      if (ourfa_xmlapictx_get_req_pkt(ctx, in, &pkt_in) != 0) {
	 set_err(ourfa, "%s", ourfa_xmlapictx_last_err_str(ctx));
	 ourfa_xmlapictx_free(ctx);
	 return -1;
      }
      if (ourfa->debug_stream != NULL)
	 ourfa_hash_dump(in, ourfa->debug_stream,
	       "FUNCTION INPUT PARAMETERS HASH ...\n");
   }

   /* Start call */
   if (ourfa_conn_start_func_call(ourfa->conn, ourfa_xmlapictx_func_id(ctx)) != 0) {
      ourfa_xmlapictx_free(ctx);
      ourfa_pkt_free(pkt_in);
      return -1;
   }

   /* Send input parameters  */
   if (pkt_in != NULL) {
      if (ourfa_pkt_add_attrs(pkt_in, "4i", 4) != 0) {
	 ourfa_xmlapictx_free(ctx);
	 ourfa_pkt_free(pkt_in);
	 return set_err(ourfa, "Cannot add termination attribute to output packet");
      }

      ourfa_pkt_dump(pkt_in, ourfa->debug_stream,
	    "SENDING FUNC INPUT PARAMS PKT ...\n");
      if (ourfa_send_packet(ourfa, pkt_in) <= 0) {
	 ourfa_xmlapictx_free(ctx);
	 ourfa_pkt_free(pkt_in);
	 return -1;
      }
   }
   ourfa_pkt_free(pkt_in);

   /* Recv and parse answer */
   /*
   if (!ourfa_xmlapictx_have_output_parameters(ctx)) {
	 ourfa_xmlapictx_free(ctx);
	 if (out)
	    *out = NULL;
	 return 0;
   }
   */

   pkt_out = NULL;
   res_h = NULL;
   last_err=1;

   resp_ctx = ourfa_xmlapictx_load_resp_pkt_start(ourfa->xmlapi, func);
   if (!resp_ctx) {
      set_err(ourfa, "Cannot init response context: %s",
	    ourfa_xmlapi_last_err_str(ourfa->xmlapi));
      ourfa_xmlapictx_free(ctx);
      /* XXX: disconnect */
      if (out)
	 *out = NULL;
      return 0;
   }

   while ((recvd_bytes=ourfa_recv_packet(ourfa, &pkt_out)) > 0) {
      ourfa_pkt_dump(pkt_out, ourfa->debug_stream, "RECIVED FUNC OUTPUT PKT ...\n");

      /* Load packet */
      if (last_err == 1) {
	 last_err = ourfa_xmlapictx_load_resp_pkt(resp_ctx, pkt_out);
      }

      /* Check for termination attribute */
      attr_list = ourfa_pkt_get_attrs_list(pkt_out, OURFA_ATTR_TERMINATION);
      if (attr_list != NULL) {
	 ourfa_pkt_free(pkt_out);
	 break;
      }

      ourfa_pkt_free(pkt_out);
   }

   res_h = ourfa_xmlapictx_load_resp_pkt_end(resp_ctx);

   /* Error while recvd packet  */
   if (recvd_bytes <=0) {
      ourfa_xmlapictx_free(ctx);
      return -1;
   }

   if (last_err < 0 || (res_h == NULL)) {
      set_err(ourfa, "Unable to parse packet: %s", ourfa_xmlapi_last_err_str(ourfa->xmlapi));
      ourfa_xmlapictx_free(ctx);
      return -1;
   }

   if ((last_err == 1) && (ourfa->debug_stream != NULL))
	 fprintf(ourfa->debug_stream, "Parser returns 'incomplete result' "
	       "error (no enough imput data packets)\n");

   if (ourfa->debug_stream != NULL)
      ourfa_hash_dump(res_h, ourfa->debug_stream, "RECIVED HASH ...\n");

   if (out)
      *out = res_h;
   else
      ourfa_hash_free(res_h);

   ourfa_xmlapictx_free(ctx);

   return 0;
}

static int hash_dump_xml(ourfa_t *ourfa, const char *func_name,
      ourfa_hash_t *h, FILE *stream, unsigned dump_input,
      enum dump_format_t dump_format)
{
   int res;

   if (ourfa == NULL)
      return -1;

   ourfa->err_msg[0]='\0';

   if (ourfa->xmlapi == NULL)
      return set_err(ourfa, "XML api not loaded");

   if (func_name == NULL)
      return set_err(ourfa, "Action not defined");

   if (stream == NULL)
      return 0;

   res=0;
   switch (dump_format) {
      case DUMP_FORMAT_XML:
	 res = ourfa_xmlapi_xml_dump(ourfa->xmlapi, func_name, h, stream, dump_input);
	 break;
      case DUMP_FORMAT_BATCH:
	 res = ourfa_xmlapi_batch_dump(ourfa->xmlapi, func_name, h, stream, dump_input);
	 break;
      default:
	 assert(0);
	 break;
   }

   if (res != 0) {
      set_err(ourfa, "%s",
	    ourfa_xmlapi_last_err_str(ourfa->xmlapi));
   }

   return res;
}

int ourfa_hash_dump_xml(ourfa_t *ourfa, const char *func_name,
      ourfa_hash_t *h, FILE *stream, unsigned dump_input)
{
   return hash_dump_xml(ourfa, func_name, h, stream, dump_input,
	DUMP_FORMAT_XML);
}

int ourfa_hash_dump_batch(ourfa_t *ourfa, const char *func_name,
      ourfa_hash_t *h, FILE *stream, unsigned dump_input)
{
   return hash_dump_xml(ourfa, func_name, h, stream, dump_input,
	DUMP_FORMAT_BATCH);
}

ourfa_xmlapi_t *ourfa_get_xmlapi(ourfa_t *ourfa)
{
   return ourfa ? ourfa->xmlapi : NULL;
}

ourfa_conn_t *ourfa_get_conn(ourfa_t *ourfa)
{
   return ourfa ? ourfa->conn : NULL;
}

