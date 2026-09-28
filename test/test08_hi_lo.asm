li $1,100000
li $2,30000

mult $1,$2
mflo $3
mfhi $4

li $5,-17
li $6,5

div $5,$6
mflo $7
mfhi $8

DUMP_PROCESSOR_STATE
