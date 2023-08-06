void lcd_init();
void lcd_print(int line, const char *fmt, ...);
void lcd_print_addr(char *msg, ble_addr_t *addr);
