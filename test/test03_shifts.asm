li $1,1
li $2,-1

sll $3,$1,0
sll $4,$1,1
sll $5,$1,31

srl $6,$2,0
srl $7,$2,1
srl $8,$2,31

li $9,-1
sll $9,$9,4
srl $10,$9,4

DUMP_PROCESSOR_STATE
