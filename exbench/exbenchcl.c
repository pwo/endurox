/**
 * @brief Benchmark tool client
 *
 * @file exbenchcl.c
 */
/* -----------------------------------------------------------------------------
 * Enduro/X Middleware Platform for Distributed Transaction Processing
 * Copyright (C) 2009-2016, ATR Baltic, Ltd. All Rights Reserved.
 * Copyright (C) 2017-2019, Mavimax, Ltd. All Rights Reserved.
 * This software is released under one of the following licenses:
 * AGPL (with Java and Go exceptions) or Mavimax's license for commercial use.
 * See LICENSE file for full text.
 * -----------------------------------------------------------------------------
 * AGPL license:
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License, version 3 as published
 * by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License, version 3
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * -----------------------------------------------------------------------------
 * A commercial use license is available from Mavimax, Ltd
 * contact@mavimax.com
 * -----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ndebug.h>
#include <atmi.h>
#include <exthpool.h>

#include "atmi_int.h"
#include "expr.h"
#include <typed_buf.h>

/*---------------------------Externs------------------------------------*/
/*---------------------------Macros-------------------------------------*/
/*---------------------------Enums--------------------------------------*/
/*---------------------------Typedefs-----------------------------------*/
/*---------------------------Globals------------------------------------*/
/*---------------------------Statics------------------------------------*/
exprivate int M_nr_threads=1;

/* if contains %d then it is replaced with thread number */
exprivate char M_svcnm[XATMI_SERVICE_NAME_LENGTH+1]="EXBENCH";
exprivate int M_runtime=60;             /**< run for 60 sec default          */
exprivate char *M_sample_data=NULL;     /**< data to send                    */
exprivate BFLDID M_fld=BBADFLDID;       /**< field used for random data fill */
exprivate int M_rndsize=1024;           /**< test data size                  */
exprivate int M_doplot=EXFALSE;         /**< Do plot the benchmark restulst  */
exprivate int M_prio=NDRX_MSGPRIO_DEFAULT;  /**< Call priorioty              */
exprivate typed_buffer_descr_t * M_buftype = NULL;
exprivate threadpool M_threads; /**< thread pool */
exprivate MUTEX_LOCKDECL(M_wait_mutex);
exprivate int M_do_run = EXTRUE;
exprivate long M_msg_sent=0;    /**< Messages sent */
exprivate char *M_master_buf=NULL;  /**< This is master sample buffer       */
exprivate int M_msgsize = 0;    /**< effective message size */
/* Lock  */
/*---------------------------Prototypes---------------------------------*/


/* need to synchronize function for starting the sending... */


expublic void thread_process(void *ptr, int *p_finish_off)
{
    /*  thread number */
    long thnum = (long)ptr;
    char svcnm[XATMI_SERVICE_NAME_LENGTH+1];
    char *buf = tpalloc(M_buftype->type, NULL, M_rndsize*2);
    char *rcv_buf;
    long rcvlen;
    long sent=0;
    
    if (NULL==buf)
    {
        NDRX_LOG(log_error, "Failed to alloc send buf: %s", 
                tpstrerror(tperrno));
        exit(-1);
    }
    
    /* prep */
    memcpy(buf, M_master_buf, M_rndsize*2);
    
    
    /* Service by thread */
    snprintf(svcnm, sizeof(svcnm), M_svcnm, (int)thnum);
    
    /* re-lock.. */
    MUTEX_LOCK_V(M_wait_mutex);
    MUTEX_UNLOCK_V(M_wait_mutex);
    
    while (M_do_run)
    {
        if (M_prio!=NDRX_MSGPRIO_DEFAULT)
        {
            tpsprio(M_prio, TPABSOLUTE);
        }
        rcv_buf=NULL;
        if (EXFAIL==tpcall(svcnm, buf, 0, &rcv_buf, &rcvlen, 0))
        {
            NDRX_LOG(log_error, "Failed to call [%s]: %s", 
                    svcnm, tpstrerror(tperrno));
            exit(-1);
        }
        
        if (NULL!=rcv_buf)
        {
            tpfree(rcv_buf);
        }
        
        sent++;
    }
    
    /* publish results... */
    MUTEX_LOCK_V(M_wait_mutex);
    M_msg_sent+=sent;
    MUTEX_UNLOCK_V(M_wait_mutex);
    
out:
    
    *p_finish_off=EXTRUE;

    /* free up ... */
    if (NULL!=buf)
    {
        tpfree(buf);
    }

    /* release resources */
    tpterm();

    return;
    
}

/**
 * Print usage
 * @param bin binary name
 */
expublic void usage(char *bin)
{
    fprintf(stderr, "Usage: %s [options] -B buffer_type \n", bin);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p <prio>        Call priority\n");
    fprintf(stderr, "  -P               Plot results, default false. Needs NDRX_BENCH_FILE and NDRX_BENCH_CONFIGNAME\n");
    fprintf(stderr, "  -n <threadsnr>   Number of threads, default 1\n");
    fprintf(stderr, "  -s <svcnm>       Service to call\n");
    fprintf(stderr, "  -t <time>        Number of seconds to run\n");
    fprintf(stderr, "  -f <fldnm>       Ubf field name to fill with random data\n");
    fprintf(stderr, "  -b <data>        Initial data. For UBF it is JSON\n");
    fprintf(stderr, "  -S <size>        Random data size, default 1024\n");
   
}

/**
 * Benchmark tool entry
 */
expublic int main( int argc, char** argv )
{
    int ret = EXSUCCEED;
    int c;
    long i;
    char *rnd_block = NULL;
    double spent;
    double tps;
    ndrx_stopwatch_t w;
    char run_ver[512];
    /* parse args: 
     * -n <number_of_threads> 
     * -s <service_to_call> 
     * -t <time_in_sec> 
     * -b <UBF buffer base> 
     * -f <random_data_field for UBF> 
     * -S <random_data_size>
     * -P - do plot
     * -p <call_priority>
     * -B <buffer type UBF, STRING, etc..)
     */
    
    while ((c = getopt (argc, argv, "n:s:t:b:r:S:p:B:Pf:")) != -1)
    {
        switch (c)
        {
            case 'B':
                M_buftype = ndrx_get_buffer_descr(optarg, NULL);
                break;
            case 'p':
                M_prio = atoi(optarg);
                break;
            case 'P':
                M_doplot = EXTRUE;

                break;
            case 'n':
                /* this will allocate thread pool... */
                M_nr_threads=atoi(optarg);
                break;
            case 's':
                /* service to call */
                NDRX_STRCPY_SAFE(M_svcnm, optarg);
                break;
            case 't':
                M_runtime = atoi(optarg);
                break;
            case 'b':
                
                M_sample_data = NDRX_STRDUP(optarg);
                
                if (NULL==M_sample_data)
                {
                    NDRX_LOG(log_error, "Failed to copy data buffer: %s", strerror(errno));
                    EXFAIL_OUT(ret);
                }
                
                break;
            case 'f':
                
                M_fld = Bfldid(optarg);
                
                if (BBADFLDID==M_fld)
                {
                    NDRX_LOG(log_error, "Failed to resolve field id [%s]: %s", 
                            Bstrerror(Berror));
                    EXFAIL_OUT(ret);
                }
                break;
            case 'S':
                M_rndsize = atoi(optarg);
                break;
                
            default:
                NDRX_LOG(log_error, "Unknown option %c", c);
                usage(argv[0]);
                EXFAIL_OUT(ret);
                break;
        }
    }
    
    
    /*** print details: ****/
    
    NDRX_LOG(log_info, "M_svcnm=[%s]", M_svcnm);
    NDRX_LOG(log_info, "M_runtime=%d", M_runtime);
    NDRX_LOG(log_info, "M_sample_data=[%s]", (M_sample_data?M_sample_data:"NULL"));
    NDRX_LOG(log_info, "M_fld=%ld", M_fld);
    NDRX_LOG(log_info, "M_rndsize=%d", M_rndsize);
    NDRX_LOG(log_info, "M_doplot=%d", M_doplot);
    NDRX_LOG(log_info, "M_prio=%d", M_prio);
    NDRX_LOG(log_info, "M_buftype=%p", M_buftype);
    NDRX_LOG(log_info, "M_nr_threads=%d", M_nr_threads);
    
    /* allocate the buffer & fill with random data */
    
    if (NULL==M_buftype)
    {
        NDRX_LOG(log_error, "Invalid buffer specified (or not specified)");
        usage(argv[0]);
        EXFAIL_OUT(ret);
    }
    
    M_master_buf = tpalloc(M_buftype->type, NULL, M_rndsize*2);
    
    if (NULL==M_master_buf)
    {
        NDRX_LOG(log_error, "Failed to allocate send buffer: %s", tpstrerror(tperrno));
        EXFAIL_OUT(ret);
    }
    
    /* parse data in ... */
    
    if (0==strcmp(M_buftype->type, "UBF"))
    {
        if (NULL!=M_sample_data)
        {
            if (EXSUCCEED!=tpjsontoubf((UBFH *)M_master_buf, M_sample_data))
            {
                NDRX_LOG(log_error, "Failed to parse call data: %s", 
                        tpstrerror(tperrno));
                EXFAIL_OUT(ret);
            }
        }
        
        /* load random data */
        rnd_block = NDRX_MALLOC(M_rndsize);
        
        if (NULL==rnd_block)
        {
            NDRX_LOG(log_error, "Failed to malloc random block: %s", 
                    strerror(errno));
            EXFAIL_OUT(ret);
        }
       
        /* carray block */
        if (BBADFLDID!=M_fld)
        {
            NDRX_LOG(log_debug, "Adding random block to %s of %d", Bfname(M_fld), M_rndsize);
            if (EXSUCCEED!=Bchg((UBFH *)M_master_buf, M_fld, 0, rnd_block, M_rndsize))
            {
                NDRX_LOG(log_error, "Failed to add random block: %s", 
                        Bstrerror(Berror));
                EXFAIL_OUT(ret);
            }
        }
        
        M_msgsize=Bused((UBFH *)M_master_buf);
    }
    
    if (!getenv("NDRX_BENCH_FILE"))
    {
        setenv("NDRX_BENCH_FILE", "test.out", EXTRUE);
    }
    
    if (!getenv("NDRX_BENCH_CONFIGNAME"))
    {
        snprintf(run_ver, sizeof(run_ver), "msg size: %d", M_msgsize);
        setenv("NDRX_BENCH_CONFIGNAME", run_ver, EXTRUE);
    }
    
    M_threads = ndrx_thpool_init(M_runtime,  &ret, NULL, NULL, 0, NULL);
        
    if (EXSUCCEED!=ret)
    {
        NDRX_LOG(log_error, "Thread pool init failure");
        EXFAIL_OUT(ret);
    }
    
    /* sync to master */
    MUTEX_LOCK_V(M_wait_mutex);
    
    for (i=0; i<M_nr_threads; i++)
    {
        /* thread nr is set as ptr */
        ndrx_thpool_add_work(M_threads, (void*)thread_process, (void *)i);
    }
    
    /* let threads to prepare */
    sleep(2);
    
    ndrx_stopwatch_reset(&w);
    
    MUTEX_UNLOCK_V(M_wait_mutex);
    
    /* let it run... */
    while (ndrx_stopwatch_get_delta_sec(&w) < M_runtime)
    {
        sleep(1);
    }
    M_do_run=EXFALSE;
    
    /* wait.. */
    ndrx_thpool_wait(M_threads);
    spent=ndrx_stopwatch_get_delta(&w);
    
    tps = ((double)M_msg_sent / (spent / 1000));
    
    NDRX_LOG(log_debug, "Spent: %ld ms msgs: %ld tps: %lf", 
            spent, M_msg_sent, tps);
    
    /* write the stats... */
    if (M_doplot)
    {
        ndrx_bench_write_stats(M_msgsize, tps);
    }
    
    ndrx_thpool_destroy(M_threads);
    
out:
        
    if (NULL!=M_master_buf)
    {
        tpfree((char *)M_master_buf);
    }

    if (NULL!=M_sample_data)
    {
        NDRX_FREE(M_sample_data);
    }

    if (NULL!=rnd_block)
    {
        NDRX_FREE(rnd_block);
    }

    return ret;
    
}

/* vim: set ts=4 sw=4 et smartindent: */