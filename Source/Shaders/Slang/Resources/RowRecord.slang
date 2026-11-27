// A buffer record read as float4 rows. GPU-assisted validation instruments every storage
// buffer load and warns above 75 per shader; copying a record field by field costs one load
// per field (and Slang re-loads a field at each use), copying its rows in a loop costs one.
//
// Bind StructuredBuffer<RowRecord<N>> where N is the record size / 16, and read with
// LoadRowRecord<T>. T must hold only 16-byte vectors (or arrays and structs of them, or
// four 4-byte scalars in a row), so its buffer layout and bit_cast layout agree; a size
// mismatch does not compile.
#ifndef SWIM_ROW_RECORD_SLANG
#define SWIM_ROW_RECORD_SLANG

struct RowRecord<let N : int>
{
	float4 Rows[N];
};

T LoadRowRecord<T, let N : int>(StructuredBuffer<RowRecord<N>> buffer, uint index)
{
	float4 rows[N];
	for (uint i = 0; i < N; ++i)
	{
		rows[i] = buffer[index].Rows[i];
	}
	return bit_cast<T>(rows);
}

#endif
