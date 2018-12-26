int kermit(short, struct k_data *, short, int, char *, struct k_response *);
UCHAR * getrslot(struct k_data *, short *);
UCHAR * getsslot(struct k_data *, short *);
void freerslot(struct k_data *, short);
void freesslot(struct k_data *, short);
