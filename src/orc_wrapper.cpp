/*-------------------------------------------------------------------------
 *
 * orc_wrapper.cpp
 *    An encapsulator for ORC file reader/write functions
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Contains functions that wrap Apache ORC library functions to be used
 * by ORC FDW.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    src/orc_wrapper.cpp
 *
 *-------------------------------------------------------------------------
 */

/* ORC FDW header files */
#include <orc_wrapper.h>
#include <orc_interface_typedefs.h>

/* Apache ORC header files */
#include <orc/Exceptions.hh>
#include <orc/OrcFile.hh>

/* PostgreSQL and FDW header files */
extern "C"
{
    #include "c.h"
    #include "orc_fdw.h"
}


/* Declare the functions to use within this file */
static std::string IsSupportedVersion(ORC_UNIQUE_PTR<orc::Reader> *p_reader);


/*
 * orcCreateReader
 *    Creates Apache ORC file reader for file in a safe way for fdw.
 *    Creates a reader for the specified filename and stores it in
 *    the unique_ptr in p_reader.
 */
bool
orcCreateReader(std::string filename,
                    ORC_UNIQUE_PTR<orc::Reader> *p_reader, 
                    orc::ReaderOptions &options,
                    bool blnVersionWarn)
{
    /* Let's catch exceptions and throw an error */
    try
    {
        ORC_UNIQUE_PTR<orc::InputStream> inStream =
            orc::readLocalFile(filename.c_str());

        *p_reader = orc::createReader(std::move(inStream), options);
    }
    catch (orc::ParseError& err)
    {
        ereport(ERROR, (errmsg("%s: %s", ORC_FDW_NAME, err.what())));
    }

    /* Throw a warning for unsupported ORC version */
    std::string fileVersion = IsSupportedVersion(p_reader);
    if (fileVersion.empty() == false && blnVersionWarn )
    {
        ereport(WARNING, (errmsg("%s: Unsupported ORC file %s version 0.11.", ORC_FDW_NAME, filename.c_str()),
                         (errhint("This may still work, but it's strongly recommended to use files that are supported by the fdw."))));
    }

    /* *p_reader should never by NULL here, but just in case */
    return ((*p_reader) != NULL);
}

/*
 * orcCreateRowReader
 *    Creates Apache ORC file row reader in safe way for fdw.
 */
bool
orcCreateRowReader(ORC_UNIQUE_PTR<orc::Reader> *p_reader,
                    ORC_UNIQUE_PTR<orc::RowReader> *p_rowReader, 
                    orc::RowReaderOptions &rowReaderOptions)
{
    *p_rowReader = (*p_reader)->createRowReader(rowReaderOptions);

    if (*p_rowReader == NULL)
    {
        ereport(ERROR, (errmsg("%s: Unable to create row reader for ORC file.", ORC_FDW_NAME)));
    }

    return true;
}

/*
 * orcGetNumberOfRows
 *    Returns number of rows in the ORC file.
 */
uint64_t
orcGetNumberOfRows(ORC_UNIQUE_PTR<orc::Reader> *p_reader)
{
    return (*p_reader)->getNumberOfRows();
}

/*
 * orcGetColsInfo
 *    Get column meta data and return an std::vector of OrcFileColInfo.
 */
std::vector<OrcFileColInfo>
orcGetColsInfo(std::string file_pathname, ORC_UNIQUE_PTR<orc::Reader> *p_reader, orc::StructVectorBatch **p_root)
{
	orc::ReaderOptions options;
    (void) orcCreateReader(file_pathname, p_reader, options, true);

	orc::RowReaderOptions rowReaderOptions;
	ORC_UNIQUE_PTR<orc::RowReader> rowReader;
    (void) orcCreateRowReader(p_reader, &rowReader, rowReaderOptions);

    /* Check for batch for reading */
	ORC_UNIQUE_PTR<orc::ColumnVectorBatch> batch = rowReader->createRowBatch(1);
    if (batch == NULL)
    {
        ereport(ERROR, (errmsg("%s: Unable to create row batch for reading.", ORC_FDW_NAME)));
    }

    /* Get the batch */
    *p_root = dynamic_cast<orc::StructVectorBatch *>(batch.get());
    if ((*p_root) == NULL)
    {
        ereport(ERROR, (errmsg("%s: Unable to get batch from ORC file.", ORC_FDW_NAME)));
    }

    /* Return column information std::tuple */
    return orcGetColsInfo(p_reader, &rowReader, *p_root);
}

/*
 * orcGetColsInfo
 *    Fills and returns a vector of tuples with column meta data.
 */
std::vector<OrcFileColInfo>
orcGetColsInfo(ORC_UNIQUE_PTR<orc::Reader> *p_reader, ORC_UNIQUE_PTR<orc::RowReader> *p_rowReader, orc::StructVectorBatch *root)
{
    std::vector<OrcFileColInfo> col_list;

    // for (uint col_index = 0; col_index < (*p_rowReader)->getSelectedType().getSubtypeCount(); col_index++)
    for (uint col_index = 0; col_index < root->fields.size(); col_index++)
    {
        OrcFileColInfo col;
        auto orc_col_id = (*p_rowReader)->getSelectedType().getSubtype(col_index)->getColumnId();

        col.hasNull = (*p_reader)->getColumnStatistics(orc_col_id)->hasNull();
        col.kind = (*p_rowReader)->getSelectedType().getSubtype(col_index)->getKind();
        col.max_length = (*p_rowReader)->getSelectedType().getSubtype(col_index)->getMaximumLength();
        col.precision = (*p_rowReader)->getSelectedType().getSubtype(col_index)->getPrecision();
        col.scale = (*p_rowReader)->getSelectedType().getSubtype(col_index)->getScale();

        /* Index must be fixed if the rowReader was created for specific columns */
        col.index = col_index;
        col.name = (*p_rowReader)->getSelectedType().getFieldName(col_index);

        col_list.push_back(col);
    }

    return col_list;
}

/*
 * IsSupportedVersion
 *    To be used internally in this file, for a supported version, returns
 *    emptry string otherwise returns the ORC format version of file
 *    as string to be used in an message.
 */
static
std::string IsSupportedVersion(ORC_UNIQUE_PTR<orc::Reader> *p_reader)
{
    if ((*p_reader)->getFormatVersion() != orc::FileVersion(0, 12))
    {
        return (*p_reader)->getFormatVersion().toString();
    }

    return std::string("");
}

/*
 * orcGetDefaultDecimalScale
 *    ORC version 0.11 does not define decimal places for a decimal
 *    value. This function handles that. Although we don't support
 *    version 0.11, but having this one additional check improves
 *    0.11 support in FDW.
 *
 *    Returns 6 which is the default number of decimal places in
 *    0.11 version. Otherwise, return empty so that we can pick
 *    number of decimal places from schema.
 */
int
orcGetDefaultDecimalScale(ORC_UNIQUE_PTR<orc::Reader> *p_reader)
{
    std::string fileVersion = IsSupportedVersion(p_reader);

    /* Let's assume that it 0.11 */
    if (fileVersion.empty() == false)
        return 6;
    else
        return 0;
}
