#ifdef VERBOSE
void PrintMillis(String label)
{
  Serial.print(label);
  Serial.println(millis());
}
#endif

#if defined(ERROR) || defined(VERBOSE)
void serialWriteDebug(byte buf[], int len)
{
  Serial.print(buf[0],HEX);
  for(int i=1; i<len; i++)
  {
    Serial.print(",");
    Serial.print(buf[i],HEX);
  }
}
#endif
