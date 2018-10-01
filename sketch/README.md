<pre>
Scan 1-Wire bus
@ : 28 65 DC 33 04 00 00 17 = DS18B20

> 

> h

    ___          _       _
   / _ \        | |     (_)
  / /_\ \_ __ __| |_   _ _ _ __   ___
  |  _  | '__/ _` | | | | | '_ \ / _ \
  | | | | | | (_| | |_| | | | | | (_) |
  \_| |_/_|  \__,_|\__,_|_|_| |_|\___/

Commands available :
  pin <pin number [4-8]> <0,1> - set pin value
  h|help                       - help
  s|io|status                  - i/o status
  t|temp                       - temperature
  p|pressure                   - pressure
  load1 <val>                  - set load (1 min) value (log scaled)
  load5 <val>                  - set load (5 min) value (log scaled)
  disk <val>                   - set disk value

> load1 50
OK

> load5 30
OK

> disk 70
OK

> p
Temperature = 25.70 *C
Pressure = 102352 Pa
   

> t

Temperature :
ROM [ 28 65 DC 33 04 00 00 17 ] = 25.63 °C
   

> 
</pre>
