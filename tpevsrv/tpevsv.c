/* 
** Event Broker services
**
** @file tpevsv.c
** 
** -----------------------------------------------------------------------------
** Enduro/X Middleware Platform for Distributed Transaction Processing
** Copyright (C) 2015, Mavimax, Ltd. All Rights Reserved.
** This software is released under one of the following licenses:
** GPL or Mavimax's license for commercial use.
** -----------------------------------------------------------------------------
** GPL license:
** 
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; either version 2 of the License, or (at your option) any later
** version.
**
** This program is distributed in the hope that it will be useful, but WITHOUT ANY
** WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
** PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along with
** this program; if not, write to the Free Software Foundation, Inc., 59 Temple
** Place, Suite 330, Boston, MA 02111-1307 USA
**
** -----------------------------------------------------------------------------
** A commercial use license is available from Mavimax, Ltd
** contact@mavimax.com
** -----------------------------------------------------------------------------
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <regex.h>
#include <utlist.h>

#include <ndebug.h>
#include <atmi.h>
#include <atmi_int.h>
#include <typed_buf.h>
#include <ndrstandard.h>
#include <ubf.h>
#include <Exfields.h>
#include <atmi_shm.h>
#include <exregex.h>
#include "tpevsv.h"
/*---------------------------Externs------------------------------------*/
/*---------------------------Macros-------------------------------------*/
/*---------------------------Enums--------------------------------------*/
/*---------------------------Typedefs-----------------------------------*/
/*---------------------------Globals------------------------------------*/
private event_entry_t *M_subscribers=NULL;
/*---------------------------Statics------------------------------------*/
/*---------------------------Prototypes---------------------------------*/

/**
 * 
 * @param subscription
 * @param my_id
 * @return
 */
private long remove_by_my_id (long subscription, char *my_id)
{
    event_entry_t *elt, *tmp;
    long deleted  = 0;

    /* Delete the stuff out */
    DL_FOREACH_SAFE(M_subscribers,elt,tmp)
    {
        NDRX_LOG(log_debug, "Checking Nr: %d, my_id: %s",
                                elt->subscriberNr, elt->my_id);

        if ((-1==subscription &&
                !(elt->flags & TPEVPERSIST) &&
                (NULL==my_id || (0==strcmp(elt->my_id, my_id)))
             ) ||
             subscription==elt->subscriberNr
            )
        {
            NDRX_LOG(log_debug, "Removing subscription %ld", subscription);
            /* Un-initalize  */
            ndrx_regfree(&elt->re);
            /* Delete out it from list */
            DL_DELETE(M_subscribers,elt);
            NDRX_FREE(elt);
            deleted++;
        }

    }

    return deleted;
}

/**
 * Compile the regular expression rules
 * @param regex entry
 * @return SUCCEED/FAIL
 */
private int compile_eventexpr(event_entry_t *p_ee)
{
    return ndrx_regcomp(&(p_ee->re), p_ee->eventexpr);
}

/**
 * Do dispatch over bridges is required or not??
 * @param p_svc
 * @param dispatch_over_bridges
 */
private void process_postage(TPSVCINFO *p_svc, int dispatch_over_bridges)
{
       int ret=SUCCEED;
    char *data = p_svc->data;
    event_entry_t *elt, *tmp;
    long numdisp = 0;
    
    char buf_type[9];
    char buf_subtype[17];
    long buf_len;
    tp_command_call_t * last_call;
    
    memset(buf_type, 0, sizeof(buf_type));
    memset(buf_subtype, 0, sizeof(buf_subtype));
    
    NDRX_LOG(log_debug, "process_postage got call");
    
    if (NULL!=data)
    {
        buf_len = tptypes(data, buf_type, buf_subtype);
        
        if (strcmp(buf_type, BUF_TYPE_UBF_STR) && 
                debug_get_ndrx_level() > log_debug)
        {
            Bfprint((UBFH *)data, stderr);
        }
    }
    
    last_call=ndrx_get_G_last_call();

    NDRX_LOG(log_debug, "Posting event [%s] to system", last_call->extradata);
    
    /* Delete the stuff out */
    DL_FOREACH_SAFE(M_subscribers,elt,tmp)
    {
        /* Get type */
        typed_buffer_descr_t *descr;
        descr = &G_buf_descr[last_call->buffer_type_id];

        NDRX_LOG(log_debug, "Checking Nr: %d, event [%s]",
                                elt->subscriberNr, elt->eventexpr);

        /* Check do we have event match? */
        if (SUCCEED==regexec(&elt->re, last_call->extradata, (size_t) 0, NULL, 0))
        {
            NDRX_LOG(log_debug, "Event matched");
            
            /* Check do we have special rules for this? */
            if (EOS!=elt->filter[0])
            {
                NDRX_LOG(log_debug, "Using filter: [%s]", elt->filter);
            }

            /* If filter matched, or no filter, then call the service */
            if ((EOS!=elt->filter[0] && 
                       descr->pf_test(descr, p_svc->data, p_svc->len, elt->filter)) ||
                EOS==elt->filter[0])
            {
                NDRX_LOG(log_debug, "Dispatching event");
                if (elt->flags & TPEVSERVICE)
                {
                    int err;
                    NDRX_LOG(log_debug, "Calling service %s/%s in async mode",
                                                    elt->name1, elt->my_id);

                    /* todo: Call in async: Do we need to pass there original flags? */
                    if (FAIL==(err=tpacallex (elt->name1, p_svc->data, p_svc->len, 
                                    elt->flags | TPNOREPLY, elt->my_id, FAIL, TRUE)))
                    {
                        NDRX_LOG(log_error, "Failed to call service [%s/%s]: %s"
                                " - unsubscribing %ld",
                                elt->name1, elt->my_id, 
                                tpstrerror(tperrno), elt->subscriberNr);
                        /* IF NO ENT, THEN UNSUBSCIRBE!!! */
                        remove_by_my_id(elt->subscriberNr, NULL);
                    }
                    else
                    {
                        numdisp++;
			/* free up connection descriptor */
                        if (err)
                        {
                            tpcancel(err);
                        }
                    }
                }
                else
                {
                    NDRX_LOG(log_debug, "Skipping subscriber due to "
                                        "unsupported event delivery mechanism!");
                }
            }
            else
            {
                NDRX_LOG(log_debug, "Not dispatching event due to filter");
            }
        }
    }
    
    if (dispatch_over_bridges)
    {
        char nodes[CONF_NDRX_NODEID_COUNT+1] = {EOS};
        int i = 0;
        long olen;
        NDRX_LOG(log_debug, "Dispatching events over the bridges...!");
        if (SUCCEED==ndrx_shm_birdge_getnodesconnected(nodes))
        {
            while (nodes[i])
            {
                int nodeid = nodes[i];
                char *tmp_data = NULL;
                
                if (NULL!=tmp_data)
                {
                    if (buf_len < 0)
                    {
                        NDRX_LOG(log_error, "Invalid buffer type!");
                        break;
                    }

                    tmp_data = tpalloc(buf_type, 
                            (buf_subtype[0]==EOS?NULL:buf_subtype), buf_len);
                }
                else
                {
                    /* Ensure that we have output buffer: */
                    tmp_data = tpalloc(BUF_TYPE_UBF_STR, NULL, 1024);
                }
                
                if (NULL==tmp_data)
                {
                    NDRX_LOG(log_error, "Cannot deliver event to node %c"
                            " - tpalloc failed for dest buffer...");
                    break;
                }
                
                if (FAIL==(tpcallex (NDRX_SYS_SVC_PFX EV_TPEVDOPOST, p_svc->data, p_svc->len,  
                        &tmp_data, &olen,
                        0, last_call->extradata, nodeid, TPCALL_BRCALL)))
                {
                    NDRX_LOG(log_error, "Call bridge %d: [%s]: %s",
                                    nodeid, EV_TPEVDOPOST,  tpstrerror(tperrno));
                }
                else
                {
                    NDRX_LOG(log_debug, "On node %d applied %d events", 
                            nodeid, tpurcode);
                    numdisp+=tpurcode;
                }
                
                if (tmp_data)
                {
                    tpfree(tmp_data);
                }
                
                i++;
            }
        }
    }
    
out:
    tpreturn(  ret==SUCCEED?TPSUCCESS:TPFAIL,
                numdisp,
                NULL,
                0L,
                0L);
}

/* TODO: We should have some stuff here. 
 * Function if it gets direct call, then dispatch it over the bridges
 * and then process locally.
 */

/**
 * Do the actual posting here...
 * This entry point for processing or entry point for bridged calls.
 * @param p_svc
 */
void TPEVDOPOST(TPSVCINFO *p_svc)
{
    process_postage(p_svc, FALSE);
}

/**
 * Post the event to subscribers
 * Event name carried in extradata of tpcallex()
 * This is entry point for clients/servers doing postage.
 * --------------------------------
 *
 * @param p_svc
 */
void TPEVPOST (TPSVCINFO *p_svc)
{
    /* We dispatch calls over the all bridges! */
    process_postage(p_svc, TRUE);
}

/**
 * Subscribe to event
 * EV_SUBSNR - subscriberNr
 * --------------------------------
 *
 * @param p_svc
 */
void TPEVUNSUBS (TPSVCINFO *p_svc)
{
    int ret=SUCCEED;
    UBFH *p_ub = (UBFH *)p_svc->data;
    long subscriberNr = 0;
    long deleted = 0;
    
    NDRX_LOG(log_debug, "EX_EVUNSUBS got call");
    Bfprint(p_ub, stderr);


    /* This field must be here! */
    if (FAIL==CBget(p_ub, EV_SUBSNR, 0, (char *)&subscriberNr, NULL, BFLD_LONG))
    {
        NDRX_LOG(log_error, "Failed to get EV_SUBSNR/subscriberNr: %s",
                                        Bstrerror(Berror));
        ret=FAIL;
        goto out;
    }
    NDRX_LOG(log_debug, "About to remove subscription: %ld, my_id: %s",
                                        subscriberNr, ndrx_get_G_last_call()->my_id);
   /* Delete the subscriptions */
   deleted=remove_by_my_id(subscriberNr, ndrx_get_G_last_call()->my_id);
    
out:
    tpreturn(  ret==SUCCEED?TPSUCCESS:TPFAIL,
                deleted,
                NULL,
                0L,
                0L);
}

/**
 * Subscribe to event
 * EV_MASK - event mask (char 255)
 * EV_FILTER - filter (char 255)
 * EV_FLAGS - flags
 * -- Part of TPEVCTL --
 * EV_SRVCNM - name1 (service name)
 * --------------------------------
 * 
 * @param p_svc
 */
void TPEVSUBS (TPSVCINFO *p_svc)
{
    int ret=SUCCEED;
    UBFH *p_ub = (UBFH *)p_svc->data;
    event_entry_t *p_ee;
    BFLDLEN len;
    static long subscriberNr = 0;
    
    NDRX_LOG(log_debug, "TPEVSUBS got call");
    Bfprint(p_ub, stderr);
    
    if (NULL==(p_ee=NDRX_MALLOC(sizeof(event_entry_t))))
    {
        NDRX_LOG(log_error, "Failed to allocate %d bytes: %s!",
                                        sizeof(event_entry_t), strerror(errno));
        ret=FAIL;
        goto out;
    }

    memset((char *)p_ee, 0, sizeof(event_entry_t));

    strcpy(p_ee->my_id, ndrx_get_G_last_call()->my_id);
    len=sizeof(p_ee->eventexpr);
    if (Bpres(p_ub, EV_MASK, 0) && FAIL==Bget(p_ub, EV_MASK, 0,
                            p_ee->eventexpr, &len))
    {
        NDRX_LOG(log_error, "Failed to get EV_MASK/eventexpr: %s",
                                        Bstrerror(Berror));
        ret=FAIL;
        goto out;
    }

    len=sizeof(p_ee->filter);
    if (Bpres(p_ub, EV_FILTER, 0) && FAIL==Bget(p_ub, EV_FILTER, 0,
                            p_ee->filter, &len))
    {
        NDRX_LOG(log_error, "Failed to get EV_FILTER/filter: %s",
                                        Bstrerror(Berror));
        ret=FAIL;
        goto out;
    }
    
    /* This field must be here! */
    if (FAIL==CBget(p_ub, EV_FLAGS, 0, (char *)&p_ee->flags, NULL, BFLD_LONG))
    {
        NDRX_LOG(log_error, "Failed to get EV_FLAGS/flags: %s",
                                        Bstrerror(Berror));
        ret=FAIL;
        goto out;
    }
    
    /* check do we support the call? */
    if (p_ee->flags & TPEVSERVICE)
    {
        len=sizeof(p_ee->name1);
        if (SUCCEED!=CBget(p_ub, EV_SRVCNM, 0, p_ee->name1, &len, BFLD_STRING))
        {
            NDRX_LOG(log_error, "Failed to get EV_SRVCNM/name1: %s",
                                            Bstrerror(Berror));
            ret=FAIL;
            goto out;
        }
    }
    else
    {
        NDRX_LOG(log_error, "tpsubscribe() unsupported flags: %ld",
                                        p_ee->flags);
        ret=FAIL;
        goto out;
    }

    /* Compile the regular expression */
    if (SUCCEED!=compile_eventexpr(p_ee))
    {
        ret=FAIL;
        goto out;
    }
    p_ee->subscriberNr=subscriberNr; /* start with 0 */

    /* Dump the key info */
    NDRX_LOG(log_debug, "Nr: %ld, event [%s], filter [%s], name1 [%s], flags [%ld]",
                            p_ee->subscriberNr, p_ee->eventexpr, p_ee->filter,
                            p_ee->name1, p_ee->flags);
    subscriberNr++; /* Increase subscription's ID for next user */

    /* Register the subscriber */
    DL_APPEND(M_subscribers, p_ee);

out:
    tpreturn(  ret==SUCCEED?TPSUCCESS:TPFAIL,
                p_ee->subscriberNr,
                NULL,
                0L,
                0L);
}

/*
 * Do initialization
 */
int NDRX_INTEGRA(tpsvrinit)(int argc, char **argv)
{
    int ret=SUCCEED;

    NDRX_LOG(log_debug, "tpsvrinit called");

    if (SUCCEED!=tpadvertise(NDRX_SYS_SVC_PFX EV_TPEVSUBS, TPEVSUBS))
    {
        NDRX_LOG(log_error, "Failed to initialize TPEVSUBS!");
        ret=FAIL;
        goto out;
    }

    if (SUCCEED!=tpadvertise(NDRX_SYS_SVC_PFX EV_TPEVUNSUBS, TPEVUNSUBS))
    {
        NDRX_LOG(log_error, "Failed to initialize TPEVUNSUBS!");
        ret=FAIL;
        goto out;
    }

    if (SUCCEED!=tpadvertise(NDRX_SYS_SVC_PFX EV_TPEVPOST, TPEVPOST))
    {
        NDRX_LOG(log_error, "Failed to initialize TPEVPOST!");
        ret=FAIL;
        goto out;
    }
    
    if (SUCCEED!=tpadvertise(NDRX_SYS_SVC_PFX EV_TPEVDOPOST, TPEVDOPOST))
    {
        NDRX_LOG(log_error, "Failed to initialize TPEVDOPOST!");
        ret=FAIL;
        goto out;
    }

out:
    return ret;
}

void NDRX_INTEGRA(tpsvrdone) (void)
{

}

