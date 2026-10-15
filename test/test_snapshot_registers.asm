li $3,7
li $4,5
add $5,$3,$4
snapshot ./test/test_snapshot_registers.bin
li $6,-2
sub $7,$5,$6
mul $8,$7,$3
DUMP_PROCESSOR_STATE
