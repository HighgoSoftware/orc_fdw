# ORC Foreign Data Wrapper for PostgreSQL

ORC foreign data wrapper (FDW) for PostgreSQL allows users to work with ORC file format from within PostgreSQL and run queries,
build reports and use other tools for better using data stored in offline ORC file format.


**NOTE:**

This release is not for production systems. It is an initial release to identify community needs and update feature list to better
suit majority of users.


## [ORC File Format](https://orc.apache.org/)
ORC (Optimized Row Columnar) file format provides a way of storing Hive data. It is an upgrade on the previous storage formats
of RC and Parquet. ORC provides a highly efficient and optimized way of storing Hive data by handling the shortcomings of other
formats. It improves performance for reading, writing and data processing for Hive. With so many benefits of this format, it is
now a popular file format for storing columnar data. Hence, the need to create a foreign data wrapper that can read and write to
ORC format.

This version supports **ORC format version 0.12**. Although we've seen 0.11 work fine as well, but we do not officially support it.


## Build Instructions
For build instructions, please see the **[build documentation](build/Readme.md)** for details.


## Functionality
This version of ORC FDW supports basic read functionality with target list pushdown.

### Data Types
Following are the supported data types at the moment.

| ORC Data Type | PostgreSQL Data Type |
| --- | --- |
| bigint (64 bit) | bigint |
| binary | bytea |
| boolean (1 bit) | boolean |
| char | char |
| date | date |
| double (64 bit) | float8 (double) |
| float (32 bit) | float4 |
| int (32 bit) | int |
| smallint (16 bit) | smallint |
| string | text |
| timestamp | timestamp without time zone |
| tinyint (8 bit) | bit |
| varchar | varchar |

## Usage
Let's start with creating the extension and server for ORC FDW.
```
CREATE EXTENSION orc_fdw;
CREATE SERVER orc_srv FOREIGN DATA WRAPPER orc_fdw;
```
Next, we either import schema or create table manually.
```
IMPORT
FOREIGN SCHEMA :"orc_sample_dir"
FROM    SERVER orc_srv
INTO    public;
```
where :"orc_sample_dir" is full path to directory containing orc files. If you want to learn more about the *IMPORT FOREIGN SCHEMA*
command details, please refer to [PostgreSQL documentation](https://www.postgresql.org/docs/current/sql-importforeignschema.html).

The system will automatically scan the specified directory (not traversing through subdirectories) and import all orc files. Any
unsupported columns will be ignored during the schema import process. In case you wish to exclude some files or include some, please
use the *LIMIT TO* or *EXCEPT* clauses.

To create an ORC FDW foreign table directly, please use the following command:
```
CREATE FOREIGN TABLE myfile
(
    x       INT
    , y     INT
)
SERVER orc_srv OPTIONS
(
    FILENAME :'file_myfile'
);
```
Currently the only supported option for creating foreign table is "filename". You may specify the table schema according to
the mapping required. However, do note that failure to map columns correctly (by providing incorrect data type) will cause
FDW to throw an error when issuing select for the foreign table.

**NOTE:**

It is really important to note that system will consider any files *.orc* extension for schema import or filename validation.

You may get the FDW version by issuing the following command:
```
select orc_fdw_version();
```

## Todo
- [x] Read functionality for ORC files
- [ ] Allow joins between ORC foreign tables
- [ ] Complete pushdown functionality to optimize plan and reads
- [ ] Complete DML functionality to allow INSERT/UPDATE/DELETE operations
- [ ] Performance specific feature implementation to speed up read and write operations

## How to Contribute
If you wish to contribute to the project directly, you may create a pull request. For bigger undertakings, please feel free to reach out to @HighgoSoftware or our development lead for this project @engineeredvirus.

## Support
ORC FDW is project of Highgo Software Inc. It is maintained and supported our development team.

## Copyrights
Copyright (c) 2019 - 2020, Highgo Software Inc.
