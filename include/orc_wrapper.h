/*-------------------------------------------------------------------------
 *
 * orc_wrapper.h
 *    An encapsulator for ORC file reader/write functions
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    include/orc_wrapper.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __ORC_WRAPPER_H
#define __ORC_WRAPPER_H

/* C++ header files */
#include <string>
#include <vector>

/* ORC FDW header files */
#include <orc_interface_typedefs.h>

bool orcCreateReader(std::string filename, 
                    ORC_UNIQUE_PTR<orc::Reader> *p_reader, 
                    orc::ReaderOptions &options,
                    bool blnVersionWarn);
bool orcCreateRowReader(ORC_UNIQUE_PTR<orc::Reader> *p_reader, 
                    ORC_UNIQUE_PTR<orc::RowReader> *p_rowReader, 
                    orc::RowReaderOptions &rowReaderOptions);


std::vector<OrcFileColInfo> orcGetColsInfo(std::string file_pathname, ORC_UNIQUE_PTR<orc::Reader> *p_reader, orc::StructVectorBatch **p_root);
std::vector<OrcFileColInfo> orcGetColsInfo(ORC_UNIQUE_PTR<orc::RowReader> *p_rowReader, orc::StructVectorBatch *root);
uint64_t orcGetNumberOfRows(ORC_UNIQUE_PTR<orc::Reader> *p_reader);
int orcGetDefaultDecimalScale(ORC_UNIQUE_PTR<orc::Reader> *p_reader);

#endif
