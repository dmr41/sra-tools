/*===========================================================================
 *
 *                            PUBLIC DOMAIN NOTICE
 *               National Center for Biotechnology Information
 *
 *  This software/database is a "United States Government Work" under the
 *  terms of the United States Copyright Act.  It was written as part of
 *  the author's official duties as a United States Government employee and
 *  thus cannot be copyrighted.  This software/database is freely available
 *  to the public for use. The National Library of Medicine and the U.S.
 *  Government have not placed any restriction on its use or reproduction.
 *
 *  Although all reasonable efforts have been taken to ensure the accuracy
 *  and reliability of the software and data, the NLM and the U.S.
 *  Government do not and cannot warrant the performance or results that
 *  may be obtained by using this software or data. The NLM and the U.S.
 *  Government disclaim all warranties, express or implied, including
 *  warranties of performance, merchantability or fitness for any particular
 *  purpose.
 *
 *  Please cite the author in any work or product based on this material.
 *
 * ===========================================================================
 * 
 */

#include "general-loader.vers.h"

#include "general-loader.hpp"

#include <sysalloc.h>

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <kapp/main.h>
#include <kapp/args.h>

#include <klib/out.h>
#include <klib/rc.h>
#include <klib/log.h>

#include <kns/stream.h>

static char const option_include_paths[] = "include";
#define OPTION_INCLUDE_PATHS option_include_paths
#define ALIAS_INCLUDE_PATHS  "I"
static
char const * include_paths_usage[] = 
{
    "Additional directories to search for schema include files. Can specify multiple paths separated by ':'.",
    NULL
};

static char const option_schemas[] = "schema";
#define OPTION_SCHEMAS option_schemas
#define ALIAS_SCHEMAS  "S"
static
char const * schemas_usage[] = 
{
    "Schema file to use. Can specify multiple files separated by ':'.",
    NULL
};

OptDef Options[] = 
{
    /* order here is same as in param array below!!! */                 
                                                                      /* max#,  needs param, required */
    { OPTION_INCLUDE_PATHS, ALIAS_INCLUDE_PATHS,    NULL, include_paths_usage,  0,  true,        false },
    { OPTION_SCHEMAS,       ALIAS_SCHEMAS,          NULL, schemas_usage,        0,  true,        false },
};

const char* OptHelpParam[] =
{
    /* order here is same as in OptDef array above!!! */
    "path(s)",
    "path(s)",
};

rc_t UsageSummary (char const * progname)
{
    return KOutMsg (
        "Usage:\n"
        "\t%s [options] \n"
        "\n"
        "Summary:\n"
        "\tPopulate a VDB database from standard input\n"
        "\n"
        ,progname);
}

char const UsageDefaultName[] = "general-loader";

rc_t CC Usage (const Args * args)
{
    rc_t rc;
    const char * progname = UsageDefaultName;
    const char * fullpath = UsageDefaultName;

    if (args == NULL)
        rc = RC (rcApp, rcArgv, rcAccessing, rcSelf, rcNull);
    else
        rc = ArgsProgram (args, &fullpath, &progname);
    if (rc)
        progname = fullpath = UsageDefaultName;

    UsageSummary (progname);
    
    const size_t argsQty = sizeof(Options) / sizeof(Options[0]);
    for(size_t i = 0; i < argsQty; i++ ) {
        if( Options[i].required && Options[i].help[0] != NULL ) {
            HelpOptionLine(Options[i].aliases, Options[i].name, OptHelpParam[i], Options[i].help);
        }
    }
    OUTMSG(("\nOptions:\n"));
    for(size_t i = 0; i < argsQty; i++ ) {
        if( !Options[i].required && Options[i].help[0] != NULL ) {
            HelpOptionLine(Options[i].aliases, Options[i].name, OptHelpParam[i], Options[i].help);
        }
    }
    
    HelpOptionsStandard ();
    HelpVersion (fullpath, KAppVersion());
    return rc;
}

uint32_t CC KAppVersion (void)
{
    return GENERAL_LOADER_VERS;
}

rc_t CC KMain (int argc, char * argv[])
{
    Args * args;
    uint32_t pcount;
    rc_t rc = ArgsMakeAndHandle (&args, argc, argv, 1, Options, sizeof Options / sizeof (OptDef));

    if ( rc == 0 )
    {
        rc = ArgsParamCount (args, &pcount);
        if ( rc == 0 )
        {
            if ( pcount != 0 )
            {
                rc = RC(rcApp, rcArgv, rcAccessing, rcParam, rcExcessive);
                MiniUsage (args);
            }
            else
            { 
                const KStream *std_in;
                rc = KStreamMakeStdIn ( & std_in );
                if ( rc == 0 )
                {
                    KStream* buffered;
                    rc = KStreamMakeBuffered ( &buffered, std_in, 0 /*input-only*/, 0 /*use default size*/ );
                    if ( rc == 0 )
                    {
                        GeneralLoader loader ( *std_in );
                        
                        rc = ArgsOptionCount (args, OPTION_INCLUDE_PATHS, &pcount);
                        if ( rc != 0 )
                        {
                            for ( uint32_t i = 0 ; i < pcount; ++i )
                            {
                                const char* value;
                                rc = ArgsOptionValue (args, OPTION_INCLUDE_PATHS, i, &value);
                                if ( rc != 0 )
                                {
                                    break;
                                }
                                loader . AddSchemaIncludePath ( value );
                            }
                        }
                        
                        rc = ArgsOptionCount (args, OPTION_SCHEMAS, &pcount);
                        if ( rc != 0 )
                        {
                            for ( uint32_t i = 0 ; i < pcount; ++i )
                            {
                                const char* value;
                                rc = ArgsOptionValue (args, OPTION_SCHEMAS, i, &value);
                                if ( rc != 0 )
                                {
                                    break;
                                }
                                loader . AddSchemaFile( value );
                            }
                        }
                        
                        if ( rc == 0 )
                        {
                            rc = loader . Run();
                        }
                        KStreamRelease ( buffered );
                    }
                    KStreamRelease ( std_in );
                }
            }
        }
    }
    
    if ( rc != 0) 
    {
        LOGERR ( klogErr, rc, "load failed" );
    }
    
    ArgsWhack(args);
    return rc;
}
