# SK Telecom LoRaWAN example

This is an example application based on Mbed OS LoRaWAN protocol APIs that implements the SK Telecom LoRaWAN extensions. The Mbed-OS LoRaWAN stack implementation is compliant with LoRaWAN v1.0.2 specification.

SK Telecom uses a non-standard join procedure.

1. Join using normal LoRaWAN 1.0.2 join procedure, using a 'pseudo' application key.
1. After join succeeds the device sends a `APP_KEY_ALLOC_REQ`.
1. The network responds with an `APP_KEY_ALLOC_ANS` message containinng a nonce.
1. The device sends `APP_KEY_REPORT_REQ`.
1. The network responds with `APP_KEY_REPORT_ANS`.
1. A new application key is calculated via:

    ```
    aes_encrypt(psuedo_app_key, NONCE (3 bytes) | NETID (3 bytes) | PAD16)
    ```

1. The device re-joins with this new key.

This is a very quick-n-dirty application. You need to set the device EUI, app EUI, pseudo app key, and the Net ID in `main.cpp` (Net ID should be gotten from the first join, but it's not exposed in the stack right now).

**Note:** If you're in the SKT / Wiznet Open House, set the Net ID to `1`, not `13` (public SKT network).

## Getting started

More information can be found at [mbed-os-example-lorawan](https://github.com/armmbed/mbed-os-example-lorawan).
