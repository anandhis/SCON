/*
 * Copyright (c) 2015      Intel, Inc. All rights reserved
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef ORTE_RML_OFI_REQUEST_H
#define ORTE_RML_OFI_REQUEST_H


#define TO_OFI_REQ(_ptr_ctx) \
    container_of((_ptr_ctx), orte_rml_ofi_request_t, ctx)

typedef enum {
    ORTE_RML_OFI_SEND,
    ORTE_RML_OFI_RECV,
    ORTE_RML_OFI_ACK,
    ORTE_RML_OFI_PROBE
} orte_rml_ofi_request_type_t;


struct orte_rml_ofi_msg_header_t{
	opal_process_name_t	origin;  
	opal_process_name_t	dst;
	uint32_t			seq_num;
	orte_rml_tag_t		tag;		
};
typedef struct orte_rml_ofi_msg_header_t orte_rml_ofi_msg_header_t;

typedef struct {
	opal_object_t super;
    orte_rml_send_t *send;

    /** OFI conduit_id the request will use - this is 
    *	the reference to element into the orte_rml_ofi.ofi_conduits[] **/
    uint8_t conduit_id;

    /** OFI Request type */
    orte_rml_ofi_request_type_t type;

    /** OFI context */
    struct fi_context ctx;

    /** Completion count used by blocking and/or synchronous operations */
    volatile int completion_count;
    
    /** Reference to the RML used to lookup */
    /*  source of an ANY_SOURCE Recv        */
    struct orte_rml_base_module_t* rml;

	orte_rml_ofi_msg_header_t hdr;

    /** Pack buffer */
    void *data_blob;

    /** Pack buffer size */
    size_t length;

  
    /** Flag to prevent MPI_Cancel from cancelling a started Recv request */
    volatile bool req_started;

    /** Request's tag used in case of an error. */
    uint64_t match_bits;

    /** Remote OFI address used when a Recv needs to be ACKed */
    fi_addr_t remote_addr;

    /** Parent request which needs to be ACKed (e.g. Synchronous Send) */
    struct orte_rml_ofi_request_t *parent;

    /** Pointer to Mrecv request to complete */ 
    struct mca_rml_request_t *mrecv_req;
} orte_rml_ofi_request_t;
OBJ_CLASS_DECLARATION(orte_rml_ofi_request_t);
//typedef struct orte_rml_ofi_request_t orte_rml_ofi_request_t;

#endif
