/*
 * Copyright (c) 2015      Intel, Inc. All rights reserved
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte_config.h"

#include "opal/dss/dss_types.h"
#include "opal/util/output.h"
#include "opal/mca/event/event.h"

#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/rml/base/base.h"
#include "orte/mca/rml/rml_types.h"

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_tagged.h>

#include "rml_ofi.h"


OBJ_CLASS_INSTANCE(orte_rml_ofi_request_t,
                   opal_object_t,
                   NULL, NULL);

/** Send callback */
/* [Desc] This is called from the progress fn when a send completion 
** is received in the cq
** wc [in] 	: the completion queue data entry
** ofi_send_req [in]:  ofi send request with the send msg and callback
*/
int orte_rml_ofi_send_callback(struct fi_cq_data_entry *wc,
                           orte_rml_ofi_request_t* ofi_req) 
{
    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s orte_rml_ofi_send_callback called ",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME) );
    assert(ofi_req->completion_count > 0);
    ofi_req->completion_count--;
    // call the callback fn of the sender
	ofi_req->send->status = ORTE_SUCCESS;
	ORTE_RML_SEND_COMPLETE(ofi_req->send);
	// [TODO] need to check for error before returning success
	return ORTE_SUCCESS;
}

/** Error callback */
/* [Desc] This is called from the progress fn when a send completion 
** is received in the cq
** wc [in] 	: the completion queue data entry
** ofi_send_req [in]:  ofi send request with the send msg and callback
*/
int orte_rml_ofi_error_callback(struct fi_cq_err_entry *error,
                           orte_rml_ofi_request_t* ofi_req)
{
    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s orte_rml_ofi_error_callback called ",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME) );
	switch(error->err) {
		default: 			
			/* call the send-callback fn with error and return, also return failure status */
			ofi_req->send->status = ORTE_ERR_CONDUIT_SEND_FAIL;     
			ORTE_RML_SEND_COMPLETE(ofi_req->send);
	}
	return ORTE_SUCCESS;
}

/** Recv handler */
/* [Desc] This is called from the progress fn when a recv completion 
** is received in the cq
** wc [in] 	: the completion queue data entry */
int orte_rml_ofi_recv_handler(struct fi_cq_data_entry *wc, uint8_t conduit_id) 
{
	orte_rml_ofi_msg_header_t msg_hdr;
	uint32_t msglen;
	char *data;

    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s orte_rml_ofi_recv_handler called ",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME) );
	/*copy the header and data from buffer and pass it on 
	** since this is the conduit recv buffer don't want it to be released as
	** considering re-using it, so for now copying to newly allocated *data 
	** the *data will be released by orte_rml_base functions */
	
	memcpy(&msg_hdr,wc->buf,sizeof(orte_rml_ofi_msg_header_t));
	msglen = wc->len - sizeof(orte_rml_ofi_msg_header_t);
	data = (char *)malloc(msglen);
	memcpy(data,(wc->buf+sizeof(orte_rml_ofi_msg_header_t)),msglen);

	/* Since OFI is point-to-point, no need to check if the intended destination is me
     send to RML */
	ORTE_RML_POST_MESSAGE(&msg_hdr.origin, msg_hdr.tag, msg_hdr.seq_num,data,msglen);
	
}


static void send_msg(int fd, short args, void *cbdata)
{
    orte_rml_send_request_t *req = (orte_rml_send_request_t*)cbdata;
	orte_process_name_t *peer = &(req->send.dst);
	orte_rml_tag_t tag = req->send.tag;
	char *dest_ep_name;
	size_t dest_ep_namelen = 0;
	int ret = OPAL_ERROR;
	fi_addr_t dest_fi_addr;
	orte_rml_send_t *snd;
	orte_rml_ofi_request_t* ofi_send_req = OBJ_NEW( orte_rml_ofi_request_t );
	uint8_t conduit_id = req->conduit_id;
   
 	snd = OBJ_NEW(orte_rml_send_t);
    snd->dst = *peer;
    snd->origin = *ORTE_PROC_MY_NAME;
    snd->tag = tag;
	if (NULL != req->send.iov) {
        snd->iov = req->send.iov;
        snd->count = req->send.count;
        snd->cbfunc.iov = req->send.cbfunc.iov;
    } else {
        snd->buffer = req->send.buffer;
        snd->cbfunc.buffer = req->send.cbfunc.buffer;
    }
    snd->cbdata = req->send.cbdata;

    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s send_msg_transport to peer %s at tag %d",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(peer), tag);

	
	/* get the peer address by doing modex_receive 	*/
    opal_output_verbose(10, orte_rml_base_framework.framework_output,
                         "%s calling OPAL_MODEX_RECV_STRING ", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME) );
	OPAL_MODEX_RECV_STRING(ret, OPAL_RML_OFI_FI_SOCKADDR_IN, peer , (char **) &dest_ep_name, &dest_ep_namelen);
    opal_output_verbose(10, orte_rml_base_framework.framework_output,
                         "%s  Return value from OPAL_MODEX_RECV_STRING - %d, length returned - %d", 
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ret, dest_ep_namelen);
    /*print the sockaddr - port and s_addr */
	struct sockaddr_in* ep_sockaddr = (struct sockaddr_in*) dest_ep_name;
	opal_output_verbose(1,orte_rml_base_framework.framework_output,
                       "%s obtained for peer %s port = 0x%x, InternetAddr = 0x%x  ",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),ORTE_NAME_PRINT(peer),ep_sockaddr->sin_port,
                        ep_sockaddr->sin_addr.s_addr);

	if ( OPAL_SUCCESS == ret) {
		opal_output_verbose(10, orte_rml_base_framework.framework_output,
                         "%s OPAL_MODEX_RECV succeded, %s peer ep name obtained. length=%d",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(peer), dest_ep_namelen);
		ret = fi_av_insert(orte_rml_ofi.ofi_conduits[conduit_id].av, dest_ep_name,1,&dest_fi_addr,0,NULL);
		if( 0 > ret) {	
			opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s fi_av_insert failed in send_msg()",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
			/* call the send-callback fn with error and return, also return failure status */
			snd->status = ORTE_ERR_ADDRESSEE_UNKNOWN;
			ORTE_RML_SEND_COMPLETE(snd);
			snd = NULL;
			//OBJ_RELEASE( ofi_send_req);
		}
		
	} else {
		
		opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s OPAL_MODEX_RECV failed to obtain  %s peer ep name ",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(peer));
			/* call the send-callback fn with error and return, also return failure status */
			snd->status = ORTE_ERR_ADDRESSEE_UNKNOWN;
			ORTE_RML_SEND_COMPLETE(snd);
			snd = NULL;
			//OBJ_RELEASE( ofi_send_req);
	}
	
	ofi_send_req->send = snd;
	ofi_send_req->completion_count = 1;

	/* [DESC] we want to send the pid,seqnum,tag in addition to the data 
	* 	copy all of this to header of message */
	ofi_send_req->hdr.dst = ofi_send_req->send->dst;
	ofi_send_req->hdr.origin = ofi_send_req->send->origin;
	ofi_send_req->hdr.seq_num = ofi_send_req->send->seq_num;
	ofi_send_req->hdr.tag	  = ofi_send_req->send->tag;

	/* copy the header and the buffer/iov/data to the ofi_send_req->datablob and update ofi_send_req->length*/
    ofi_send_req->length = sizeof(ofi_send_req->hdr);
	if( NULL != ofi_send_req->send->buffer) {
	     ofi_send_req->length += ofi_send_req->send->buffer->bytes_used;
	 	 ofi_send_req->data_blob = (char *)malloc(ofi_send_req->length);
	     memcpy( ofi_send_req->data_blob, &ofi_send_req->hdr, sizeof(ofi_send_req->hdr));
    	 memcpy( (ofi_send_req->data_blob + sizeof(ofi_send_req->hdr) ), 
		    	ofi_send_req->send->buffer->base_ptr, 
			    ofi_send_req->send->buffer->bytes_used);
    } else if ( NULL != ofi_send_req->send->iov) {
		for (int i=0; i < ofi_send_req->send->count; i++) {
		    ofi_send_req->length += ofi_send_req->send->iov[i].iov_len;
        }
        ofi_send_req->data_blob = (char *)malloc(ofi_send_req->length);
        memcpy( ofi_send_req->data_blob, &ofi_send_req->hdr, sizeof(ofi_send_req->hdr));
		int iovlen=0;
        for (int i=0; i < ofi_send_req->send->count; i++) {
            memcpy( (ofi_send_req->data_blob + sizeof(ofi_send_req->hdr)+ iovlen ), 
		        	ofi_send_req->send->iov[i].iov_base, 
			        ofi_send_req->send->iov[i].iov_len);
			iovlen += ofi_send_req->send->iov[i].iov_len;
        }
    } else {
        //just send the data
        ofi_send_req->length += ofi_send_req->send->count;
        ofi_send_req->data_blob = (char *)malloc(ofi_send_req->length);
        memcpy( ofi_send_req->data_blob, &ofi_send_req->hdr, sizeof(ofi_send_req->hdr));
    	memcpy( (ofi_send_req->data_blob + sizeof(ofi_send_req->hdr) ), 
		    	ofi_send_req->send->data, 
			    ofi_send_req->send->count);
   }

	/*  do the fi_send()  */
	ofi_send_req->completion_count=1;
	RML_OFI_RETRY_UNTIL_DONE(fi_send(orte_rml_ofi.ofi_conduits[conduit_id].ep,
									 ofi_send_req->data_blob,
									 ofi_send_req->length,
							         fi_mr_desc(orte_rml_ofi.ofi_conduits[conduit_id].mr_multi_recv),
									 dest_fi_addr,
								     (void *)&ofi_send_req->ctx));


    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s End of send_msg_transport. fi_send completed to peer %s with tag %d",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(peer), tag);

	// wait for completion to call the send callback fn (or) let progressfn handle this


	free(dest_ep_name);
    OBJ_RELEASE(req);
}

int orte_rml_ofi_send_transport_nb(int conduit_id,
                                              orte_process_name_t* peer,
                                              struct iovec* iov,
                                              int count, 
                                              orte_rml_tag_t tag,
                                              orte_rml_callback_fn_t cbfunc,
                                              void* cbdata)
{
    orte_rml_send_request_t *req;

    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s rml_ofi_send_transport to peer %s at tag %d",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(peer), tag);

	
	if( (0 > conduit_id) || ( conduit_id > orte_rml_ofi.conduit_open_num ) ) {
		/* Invalid conduit ID provided */
	    ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;	
	}
    if (ORTE_RML_TAG_INVALID == tag) {
        /* cannot send to an invalid tag */
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;
    }
    if (NULL == peer ||
        OPAL_EQUAL == orte_util_compare_name_fields(ORTE_NS_CMP_ALL, ORTE_NAME_INVALID, peer)) {
        /* cannot send to an invalid peer */
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;
    }
    /* get ourselves into an event to protect against
     * race conditions and threads
     */
	req = OBJ_NEW(orte_rml_send_request_t);   
	req->conduit_id = conduit_id;
    req->send.dst = *peer;
    req->send.iov = iov;
	req->send.count = count;
    req->send.tag = tag;
    req->send.cbfunc.iov = cbfunc;
    req->send.cbdata = cbdata;

    /* setup the event for the send callback */
    opal_event_set(orte_event_base, &req->ev, -1, OPAL_EV_WRITE, send_msg, req);
    opal_event_set_priority(&req->ev, ORTE_MSG_PRI);
    opal_event_active(&req->ev, OPAL_EV_WRITE, 1);

	return ORTE_SUCCESS;
}


int orte_rml_ofi_send_buffer_transport_nb(int conduit_id,
                                              orte_process_name_t* peer,
                                              struct opal_buffer_t* buffer,
                                              orte_rml_tag_t tag,
                                              orte_rml_buffer_transport_callback_fn_t cbfunc,
                                              void* cbdata)
{
	orte_rml_send_request_t *req;

    opal_output_verbose(1, orte_rml_base_framework.framework_output,
                         "%s rml_ofi_send_buffer_transport to peer %s at tag %d",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(peer), tag);

	
	if( (0 > conduit_id) || ( conduit_id > orte_rml_ofi.conduit_open_num ) ) {
		/* Invalid conduit ID provided */
	    ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;	
	}
    if (ORTE_RML_TAG_INVALID == tag) {
        /* cannot send to an invalid tag */
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;
    }
    if (NULL == peer ||
        OPAL_EQUAL == orte_util_compare_name_fields(ORTE_NS_CMP_ALL, ORTE_NAME_INVALID, peer)) {
        /* cannot send to an invalid peer */
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;
    }
    /* get ourselves into an event to protect against
     * race conditions and threads
     */
	req = OBJ_NEW(orte_rml_send_request_t);   
	req->conduit_id = conduit_id;
    req->send.dst = *peer;
    req->send.buffer = buffer;
    req->send.tag = tag;
    req->send.cbfunc.buffer = cbfunc;
    req->send.cbdata = cbdata;

    /* setup the event for the send callback */
    opal_event_set(orte_event_base, &req->ev, -1, OPAL_EV_WRITE, send_msg, req);
    opal_event_set_priority(&req->ev, ORTE_MSG_PRI);
    opal_event_active(&req->ev, OPAL_EV_WRITE, 1);

	return ORTE_SUCCESS;
}

