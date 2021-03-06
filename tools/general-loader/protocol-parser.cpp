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

#include "general-loader.hpp"

#include <klib/rc.h>
#include <klib/log.h>

#include "general-writer.h"
#include "utf8-like-int-codec.h"

using namespace std;

///////////// GeneralLoader::ProtocolParser

template <typename TEvent> 
rc_t 
GeneralLoader :: ProtocolParser :: ReadEvent ( Reader& p_reader, TEvent& p_event )
{   // read the part of p_event that is outside of p_event.dad (event header)
    if ( sizeof p_event > sizeof p_event . dad )
    {
        char * start = (char*) & p_event . dad;
        start += sizeof p_event . dad;
        return p_reader . Read ( start, sizeof p_event - sizeof p_event . dad );
    }
    return 0;
};

///////////// GeneralLoader::UnpackedProtocolParser

rc_t
GeneralLoader :: UnpackedProtocolParser :: ParseEvents ( Reader& p_reader, DatabaseLoader& p_dbLoader )
{
    rc_t rc;
    do 
    { 
        p_reader . Align (); 
        
        struct gw_evt_hdr_v1 evt_header;
        rc = p_reader . Read ( & evt_header, sizeof ( evt_header ) );    
        if ( rc != 0 )
        {
            break;
        }
        
        switch ( ncbi :: evt ( evt_header ) )
        {
        case evt_use_schema:
            {
                LogMsg ( klogInfo, "protocol-parser event: Use-Schema" );
                
                gw_2string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t schema_file_size = ncbi :: size1 ( evt );
                    size_t schema_name_size = ncbi :: size2 ( evt );
                    rc = p_reader . Read ( schema_file_size + schema_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . UseSchema ( string ( ( const char * ) p_reader . GetBuffer (), schema_file_size ), 
                                                      string ( ( const char * ) p_reader . GetBuffer () + schema_file_size, schema_name_size ) );
                    }
                }
            }
            break;

        case evt_remote_path:
            {
                LogMsg ( klogInfo, "protocol-parser event: Remote-Path" );
                
                gw_1string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t database_name_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( database_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . RemotePath ( string ( ( const char * ) p_reader . GetBuffer (), database_name_size ) );
                    }
                }
            }
            break;
            
        case evt_new_table:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: New-Table, id=$(i)", "i=%u", tableId );
                
                gw_1string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t table_name_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( table_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . NewTable ( tableId, string ( ( const char * ) p_reader . GetBuffer (), table_name_size ) );
                    }
                }
            }  
            break;
            
        case evt_new_column:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: New-Column, id=$(i)", "i=%u", columnId );
    
                gw_column_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t col_name_size  = ncbi :: name_size ( evt );
                    rc = p_reader . Read ( col_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . NewColumn ( columnId, 
                                                ncbi :: table_id ( evt ), 
                                                ncbi :: elem_bits ( evt ), 
                                                ncbi :: flag_bits ( evt ),
                                                string ( ( const char * ) p_reader . GetBuffer (), col_name_size ) );
                    }
                }
            }
            break;

        case evt_cell_data:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-Data, id=$(i)", "i=%u", columnId );
                
                gw_data_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    const DatabaseLoader :: Column* col = p_dbLoader . GetColumn ( columnId );
                    if ( col != 0 )
                    {
                        size_t elem_count = ncbi :: elem_count ( evt );
                        rc = p_reader . Read ( ( col -> elemBits * elem_count + 7 ) / 8 );   
                        if ( rc == 0 )
                        {
                            rc = p_dbLoader . CellData ( columnId, p_reader. GetBuffer (), elem_count );
                        }
                    }
                    else
                    {
                        rc = RC ( rcExe, rcFile, rcReading, rcColumn, rcNotFound );
                    }
                }
            }
            break;
            
        case evt_cell_default: 
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-Default, id=$(i)", "i=%u", columnId );
                
                gw_data_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    const DatabaseLoader :: Column* col = p_dbLoader . GetColumn ( columnId );
                    if ( col != 0 )
                    {
                        size_t elem_count = ncbi :: elem_count ( evt );
                        rc = p_reader . Read ( ( col -> elemBits * elem_count + 7 ) / 8 );   
                        if ( rc == 0 )
                        {
                            rc = p_dbLoader . CellDefault ( columnId, p_reader. GetBuffer (), elem_count );
                        }
                    }
                    else
                    {
                        rc = RC ( rcExe, rcFile, rcReading, rcColumn, rcNotFound );
                    }
                }
            }
            break;
            
        case evt_empty_default: 
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-EmptyDefault, id=$(i)", "i=%u", columnId );
                rc = p_dbLoader . CellDefault ( columnId, 0, 0 );
            }
            break;
            
        case evt_open_stream:
            LogMsg ( klogInfo, "protocol-parser event: Open-Stream" );
            rc = p_dbLoader . OpenStream ();
            break; 
            
        case evt_end_stream:
            LogMsg ( klogInfo, "protocol-parser event: End-Stream" );
            return p_dbLoader . CloseStream ();
            
        case evt_next_row:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Next-Row, id=$(i)", "i=%u", tableId );
                rc = p_dbLoader . NextRow ( tableId );
            }
            break;
            
        case evt_move_ahead:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Move-Ahead, id=$(i)", "i=%u", tableId );
    
                gw_move_ahead_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    rc = p_dbLoader . MoveAhead ( tableId, ncbi :: get_nrows ( evt ) );
                }
            }
            break;
            
        case evt_errmsg:
            {   
                LogMsg ( klogInfo, "protocol-parser event: Error-Message" );
                
                gw_1string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t message_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( message_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . ErrorMessage ( string ( ( const char * ) p_reader . GetBuffer (), message_size ) );
                    }
                }
            }
            break;
            
        default:
            pLogMsg ( klogErr, 
                      "unexpected general-loader event at $(o): $(e)", 
                      "o=%lu,e=%i", 
                      ( unsigned long ) p_reader . GetReadCount(), ( int ) ncbi :: evt ( evt_header ) );
            rc = RC ( rcExe, rcFile, rcReading, rcData, rcUnexpected );
            break;
        }
    }
    while ( rc == 0 );
    
    return rc;
}

///////////// GeneralLoader::PackedProtocolParser

template < typename T_uintXX > 
rc_t 
GeneralLoader :: PackedProtocolParser :: UncompressInt (  Reader& p_reader, uint16_t p_dataSize, int (*p_decode) ( uint8_t const* buf_start, uint8_t const* buf_xend, T_uintXX* ret_decoded )  )
{
    m_unpackingBuf . clear();
    // reserve enough for the best-packed case, when each element is represented with 1 byte
    m_unpackingBuf . reserve ( sizeof ( T_uintXX ) * p_dataSize );
    
    const uint8_t* buf_begin = reinterpret_cast<const uint8_t*> ( p_reader . GetBuffer() );
    const uint8_t* buf_end   = buf_begin + p_dataSize;
    while ( buf_begin < buf_end )
    {
        T_uintXX ret_decoded;
        int numRead = p_decode ( buf_begin, buf_end, &ret_decoded );
        if ( numRead <= 0 )
        {
            pLogMsg ( klogInfo, "protocol-parser: decode_uintXX() returned $(i)", "i=%i", numRead );
            return RC ( rcExe, rcFile, rcReading, rcData, rcCorrupt );
        }

        for ( size_t i = 0; i < sizeof ( T_uintXX ); ++i )
        {
            m_unpackingBuf . push_back ( reinterpret_cast<const uint8_t*> ( & ret_decoded ) [ i ] );
        }
        
        buf_begin += numRead;
    }
    
    return 0;
}

rc_t
GeneralLoader :: PackedProtocolParser :: ParseData ( Reader& p_reader, DatabaseLoader& p_dbLoader, uint32_t p_columnId, uint32_t p_dataSize )
{
    rc_t rc = 0;
    const DatabaseLoader :: Column* col = p_dbLoader . GetColumn ( p_columnId );
    if ( col != 0 )
    {
        rc = p_reader . Read ( p_dataSize );   
        if ( rc == 0 )
        {
            if ( col -> IsCompressed () )
            {
                switch ( col -> elemBits )
                {
                case 16: 
                    rc = UncompressInt ( p_reader, p_dataSize, decode_uint16 );
                    break;
                case 32:
                    rc = UncompressInt ( p_reader, p_dataSize, decode_uint32 );
                    break;
                case 64:
                    rc = UncompressInt ( p_reader, p_dataSize, decode_uint64 );
                    break;
                default:
                    LogMsg ( klogInfo, "protocol-parser: bad element size for packed integer" );
                    rc = RC ( rcExe, rcFile, rcReading, rcData, rcInvalid );
                    break;
                }
                if ( rc == 0 )
                {
                    rc = p_dbLoader . CellData ( p_columnId, m_unpackingBuf . data(), m_unpackingBuf . size() * 8 / col -> elemBits );
                }
            }
            else
            {
                rc = p_dbLoader . CellData ( p_columnId, p_reader. GetBuffer (), p_dataSize * 8 / col -> elemBits );
            }
        }
    }
    else
    {
        rc = RC ( rcExe, rcFile, rcReading, rcColumn, rcNotFound );
    }
    return rc;
}

rc_t 
GeneralLoader :: PackedProtocolParser :: ParseEvents( Reader& p_reader, DatabaseLoader& p_dbLoader )
{
    rc_t rc;
    do 
    { 
        struct gwp_evt_hdr_v1 evt_header;
        rc = p_reader . Read ( & evt_header, sizeof ( evt_header ) );    
        if ( rc != 0 )
        {
            break;
        }
        
        switch ( ncbi :: evt ( evt_header ) )
        {
        case evt_use_schema:
            {
                LogMsg ( klogInfo, "protocol-parser event: Use-Schema (packed)" );
                
                gwp_2string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t schema_file_size = ncbi :: size1 ( evt );
                    size_t schema_name_size = ncbi :: size2 ( evt );
                            
                    rc = p_reader . Read ( schema_file_size + schema_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . UseSchema ( string ( ( const char * ) p_reader . GetBuffer (), schema_file_size ), 
                                                      string ( ( const char * ) p_reader . GetBuffer () + schema_file_size, schema_name_size ) );
                    }
                }
            }
            break;
            
        case evt_use_schema2:
            {
                LogMsg ( klogInfo, "protocol-parser event: Use-Schema2" );
                
                gwp_2string_evt_U16_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t schema_file_size = ncbi :: size1 ( evt );
                    size_t schema_name_size = ncbi :: size2 ( evt );
                            
                    rc = p_reader . Read ( schema_file_size + schema_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . UseSchema ( string ( ( const char * ) p_reader . GetBuffer (), schema_file_size ), 
                                                string ( ( const char * ) p_reader . GetBuffer () + schema_file_size, schema_name_size ) );
                    }
                }
            }
            break;
            
        case evt_remote_path:
            {
                LogMsg ( klogInfo, "protocol-parser event: Remote-Path (packed)" );
                
                gwp_1string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t database_name_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( database_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . RemotePath ( string ( ( const char * ) p_reader . GetBuffer (), database_name_size ) );
                    }
                }
            }
            break;
        case evt_remote_path2:
            {
                LogMsg ( klogInfo, "protocol-parser event: Remote-Path2" );
                
                gwp_1string_evt_U16_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t database_name_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( database_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . RemotePath ( string ( ( const char * ) p_reader . GetBuffer (), database_name_size ) );
                    }
                }
            }
            break;
            
        case evt_new_table:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: New-Table (packed), id=$(i)", "i=%u", tableId );
                
                gwp_1string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t table_name_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( table_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . NewTable ( tableId, string ( ( const char * ) p_reader . GetBuffer (), table_name_size ) );
                    }
                }
            }  
            break;
        case evt_new_table2:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: New-Table2, id=$(i)", "i=%u", tableId );
                
                gwp_1string_evt_U16_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t table_name_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( table_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . NewTable ( tableId, string ( ( const char * ) p_reader . GetBuffer (), table_name_size ) );
                    }
                }
            }  
            break;
            
        case evt_new_column:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: New-Column (packed), id=$(i)", "i=%u", columnId );
    
                gwp_column_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t col_name_size  = ncbi :: name_size ( evt );
                    rc = p_reader . Read ( col_name_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . NewColumn ( columnId, 
                                                ncbi :: table_id ( evt ), 
                                                ncbi :: elem_bits ( evt ), 
                                                ncbi :: flag_bits ( evt ),
                                                string ( ( const char * ) p_reader . GetBuffer (), col_name_size ) );
                    }
                }
            }
            break;

        case evt_open_stream:
            LogMsg ( klogInfo, "protocol-parser event: Open-Stream (packed)" );
            rc = p_dbLoader . OpenStream ();
            break; 
            
        case evt_end_stream:
            LogMsg ( klogInfo, "protocol-parser event: End-Stream (packed)" );
            return p_dbLoader . CloseStream ();
            
        case evt_cell_data:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-Data (packed), id=$(i)", "i=%u", columnId );
                
                gwp_data_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    rc = ParseData ( p_reader, p_dbLoader, columnId, ncbi :: size ( evt ) );
                }
            }
            break;
            
        case evt_cell_data2:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-Data2, id=$(i)", "i=%u", columnId );
                
                gwp_data_evt_U16_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {   
                    rc = ParseData ( p_reader, p_dbLoader, columnId, ncbi :: size ( evt ) );
                }
            }
            break;
            
        case evt_cell_default:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-Default (packed), id=$(i)", "i=%u", columnId );
                
                gwp_data_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    const DatabaseLoader :: Column* col = p_dbLoader . GetColumn ( columnId );
                    if ( col != 0 )
                    {
                        size_t dataSize = ncbi :: size ( evt );
                        rc = p_reader . Read ( dataSize );   
                        if ( rc == 0 )
                        {
                            rc = p_dbLoader . CellDefault ( columnId, p_reader. GetBuffer (), dataSize * 8 / col -> elemBits );
                        }
                    }
                    else
                    {
                        rc = RC ( rcExe, rcFile, rcReading, rcColumn, rcNotFound );
                    }
                }
            }
            break;
            
        case evt_cell_default2:
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-Default2, id=$(i)", "i=%u", columnId );
                
                gwp_data_evt_U16_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {   // same as above - refactor
                    const DatabaseLoader :: Column* col = p_dbLoader . GetColumn ( columnId );
                    if ( col != 0 )
                    {
                        size_t dataSize = ncbi :: size ( evt );
                        rc = p_reader . Read ( dataSize );   
                        if ( rc == 0 )
                        {
                            rc = p_dbLoader . CellDefault ( columnId, p_reader. GetBuffer (), dataSize * 8 / col -> elemBits );
                        }
                    }
                    else
                    {
                        rc = RC ( rcExe, rcFile, rcReading, rcColumn, rcNotFound );
                    }
                }
            }
            break;
            
        case evt_empty_default: 
            {
                uint32_t columnId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Cell-EmptyDefault (packed), id=$(i)", "i=%u", columnId );
                rc = p_dbLoader . CellDefault ( columnId, 0, 0 );
            }
            break;
            
        case evt_next_row:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Next-Row (packed), id=$(i)", "i=%u", tableId );
                rc = p_dbLoader . NextRow ( tableId );
            }
            break;
            
        case evt_move_ahead:
            {
                uint32_t tableId = ncbi :: id ( evt_header );
                pLogMsg ( klogInfo, "protocol-parser event: Move-Ahead (packed), id=$(i)", "i=%u", tableId );
    
                gwp_move_ahead_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    rc = p_dbLoader . MoveAhead ( tableId, ncbi :: get_nrows ( evt ) );
                }
            }
            break;
            
        case evt_errmsg:
            {   
                LogMsg ( klogInfo, "protocol-parser event: Error-Message (packed)" );
                
                gwp_1string_evt_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t message_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( message_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . ErrorMessage ( string ( ( const char * ) p_reader . GetBuffer (), message_size ) );
                    }
                }
            }
            break;
            
        case evt_errmsg2:
            {   
                LogMsg ( klogInfo, "protocol-parser event: Error-Message2" );
                
                gwp_1string_evt_U16_v1 evt;
                rc = ReadEvent ( p_reader, evt );
                if ( rc == 0 )
                {
                    size_t message_size = ncbi :: size ( evt );
                    rc = p_reader . Read ( message_size );
                    if ( rc == 0 )
                    {
                        rc = p_dbLoader . ErrorMessage ( string ( ( const char * ) p_reader . GetBuffer (), message_size ) );
                    }
                }
            }
            break;

        default:
            pLogMsg ( klogErr, 
                      "unexpected general-loader event at $(o): $(e)", 
                      "o=%lu,e=%i", 
                      ( unsigned long ) p_reader . GetReadCount(), ( int ) ncbi :: evt ( evt_header ) );
            rc = RC ( rcExe, rcFile, rcReading, rcData, rcUnexpected );
            break;
        }
    }
    while ( rc == 0 );
    
    return rc;
}

