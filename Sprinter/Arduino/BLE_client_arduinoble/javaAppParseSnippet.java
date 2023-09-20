    @Override // com.sjty.blelibrary.base.impl.BaseDevice
    public void analysisData(String str, byte[] bArr, String str2) {
        BmsBleCallback bmsBleCallback;
        int hexIndex;
        int hexIndex2;
        String Bytes2HexString = StringHexUtils.Bytes2HexString(bArr);
        Log.e(getClass().getName(), "收到数据:" + Bytes2HexString);
        try {
            if (Bytes2HexString.startsWith("CCC1")) {
                BmsBleCallback bmsBleCallback2 = this.bmsBleCallback;
                if (bmsBleCallback2 != null) {
                    bmsBleCallback2.pwdCheck("01".equals(Bytes2HexString.substring(4, 6)));
                }
            } else if (Bytes2HexString.startsWith("CCC2")) {
                if (this.bmsBleCallback != null) {
                    int parseInt = Integer.parseInt(Bytes2HexString.substring(4, 6));
                    this.bmsBleCallback.pwdUpdate(parseInt == 1, parseInt);
                }
            } else {
                if (!Bytes2HexString.startsWith("CCF5") && !Bytes2HexString.startsWith("CCF6") && !Bytes2HexString.startsWith("CCF7") && !Bytes2HexString.startsWith("CCF8") && !Bytes2HexString.startsWith("CCFA")) {
                    if (Bytes2HexString.startsWith("CCF4")) {
                        if (Bytes2HexString.length() != 40) {
                            Log.e(TAG, "数据异常：" + Bytes2HexString);
                            return;
                        }
                        if (getHexIndex(Bytes2HexString, 4) == 1 || getHexIndex(Bytes2HexString, 4) == 0) {
                            this.tempSingelVList.clear();
                            resetSingleV();
                        }
                        for (int i = 0; i < 4; i++) {
                            int i2 = (i * 8) + 4;
                            try {
                                SingelV singelV = getSingelV(Bytes2HexString.substring(i2, i2 + 8));
                                this.tempSingelVList.add(singelV);
                                if (singelV.v > this.singleVMaxV) {
                                    this.singleVMaxV = singelV.v;
                                    int i3 = this.singleVMaxIndex;
                                    if (i3 > -1 && i3 != this.singleVMinIndex) {
                                        this.tempSingelVList.get(i3).statue = 0;
                                    }
                                    this.singleVMaxIndex = this.tempSingelVList.size() - 1;
                                    singelV.statue = 2;
                                }
                                if (singelV.v < this.singleVMinV) {
                                    this.singleVMinV = singelV.v;
                                    int i4 = this.singleVMinIndex;
                                    if (i4 > -1 && this.singleVMaxIndex != i4) {
                                        this.tempSingelVList.get(i4).statue = 0;
                                    }
                                    this.singleVMinIndex = this.tempSingelVList.size() - 1;
                                    singelV.statue = 1;
                                }
                            } catch (Exception unused) {
                            }
                        }
                        if (this.tempSingelVList.size() >= this.maxSingleNum) {
                            this.maxSingleNum = this.tempSingelVList.size();
                            this.bmsDeviceState.singelVList.clear();
                            this.bmsDeviceState.singelVList.addAll(this.tempSingelVList);
                            BmsBleCallback bmsBleCallback3 = this.bmsBleCallback;
                            if (bmsBleCallback3 != null) {
                                bmsBleCallback3.singleBack();
                            }
                        }
                    } else if (Bytes2HexString.startsWith("CCF")) {
                        if (Bytes2HexString.length() != 40) {
                            Log.e(TAG, "数据异常：" + Bytes2HexString);
                            return;
                        } else if (Bytes2HexString.startsWith("CCF9")) {
                            this.bmsDeviceState.COV = "01".equals(Bytes2HexString.substring(4, 6));
                            this.bmsDeviceState.FC = "01".equals(Bytes2HexString.substring(6, 8));
                            this.bmsDeviceState.SOCC = "01".equals(Bytes2HexString.substring(8, 10));
                            this.bmsDeviceState.OTC = "01".equals(Bytes2HexString.substring(10, 12));
                            this.bmsDeviceState.UTC = "01".equals(Bytes2HexString.substring(12, 14));
                            this.bmsDeviceState.BOTC = "01".equals(Bytes2HexString.substring(14, 16));
                            this.bmsDeviceState.CUV = "01".equals(Bytes2HexString.substring(16, 18));
                            this.bmsDeviceState.FD = "01".equals(Bytes2HexString.substring(18, 20));
                            this.bmsDeviceState.SOCD = "01".equals(Bytes2HexString.substring(20, 22));
                            this.bmsDeviceState.OTD = "01".equals(Bytes2HexString.substring(22, 24));
                            this.bmsDeviceState.UTD = "01".equals(Bytes2HexString.substring(24, 26));
                            this.bmsDeviceState.BOTD = "01".equals(Bytes2HexString.substring(26, 28));
                            this.bmsDeviceState.SCD = "01".equals(Bytes2HexString.substring(28, 30));
                            this.bmsDeviceState.OCD = "01".equals(Bytes2HexString.substring(30, 32));
                            this.bmsDeviceState.UVP = "01".equals(Bytes2HexString.substring(32, 34));
                            this.bmsDeviceState.OVP = "01".equals(Bytes2HexString.substring(34, 36));
                            BmsBleCallback bmsBleCallback4 = this.bmsBleCallback;
                            if (bmsBleCallback4 != null) {
                                bmsBleCallback4.ptetectionStatusBack();
                                return;
                            }
                            return;
                        } else if (Bytes2HexString.startsWith("CCF1")) {
                            String str3 = "";
                            for (int i5 = 0; i5 < 17; i5++) {
                                int i6 = (i5 * 2) + 4;
                                String substring = Bytes2HexString.substring(i6, i6 + 2);
                                Log.e(TAG, "名称结束位待定");
                                if ("0D".equals(substring)) {
                                    break;
                                }
                                str3 = str3 + ((char) Integer.parseInt(substring, 16));
                            }
                            this.bmsDeviceState.name = str3;
                            BmsBleCallback bmsBleCallback5 = this.bmsBleCallback;
                            if (bmsBleCallback5 != null) {
                                bmsBleCallback5.nameBack();
                                return;
                            }
                            return;
                        } else {
                            if (Bytes2HexString.startsWith("CCF0")) {
                                this.bmsDeviceState.soc = getHexIndex(Bytes2HexString, 32, 2);
                                if (this.bmsDeviceState.soc > 100) {
                                    this.bmsDeviceState.soc = 100;
                                }
                                this.bmsDeviceState.curr_MV = getHexIndex(Bytes2HexString, 4, 6);
                                this.bmsDeviceState.curr_MA = getHexIndex(Bytes2HexString, 10, 6, true);
                                this.bmsDeviceState.eDing_MAH = getHexIndex(Bytes2HexString, 16, 6);
                                this.bmsDeviceState.curr_MAH = getHexIndex(Bytes2HexString, 22, 6);
                                this.bmsDeviceState.loop = getHexIndex(Bytes2HexString, 28, 4);
                                Log.e(TAG, "SOC :0x" + Bytes2HexString.substring(32, 34));
                            } else if (Bytes2HexString.startsWith("CCF2")) {
                                this.bmsDeviceState.mos1 = "01".equals(Bytes2HexString.substring(4, 6));
                                this.bmsDeviceState.mos2 = "01".equals(Bytes2HexString.substring(6, 8));
                                int hexIndex3 = getHexIndex(Bytes2HexString, 8);
                                this.bmsDeviceState.temperature1Statue = hexIndex3 > 0;
                                this.bmsDeviceState.temperature2Statue = hexIndex3 > 1;
                                this.bmsDeviceState.temperature3Statue = hexIndex3 > 2;
                                this.bmsDeviceState.temperature4Statue = hexIndex3 > 3;
                                this.bmsDeviceState.temperature1 = getHexIndex(Bytes2HexString, 10, 4, true);
                                this.bmsDeviceState.temperature2 = getHexIndex(Bytes2HexString, 14, 4, true);
                                this.bmsDeviceState.temperature3 = getHexIndex(Bytes2HexString, 18, 4, true);
                                this.bmsDeviceState.temperature4 = getHexIndex(Bytes2HexString, 22, 4, true);
                                int hexIndex4 = getHexIndex(Bytes2HexString, 26, 2, false);
                                if (hexIndex4 > 0) {
                                    this.bmsDeviceState.tempUnit = hexIndex4;
                                }
                            } else if (Bytes2HexString.startsWith("CCF3")) {
                                this.bmsDeviceState.year = "0727" + getHexIndex(Bytes2HexString, 4) + "89" + (getHexIndex(Bytes2HexString, 6) > 9 ? Integer.valueOf(hexIndex) : "0" + hexIndex) + "92" + (getHexIndex(Bytes2HexString, 8) > 9 ? Integer.valueOf(hexIndex2) : "0" + hexIndex2);
                                this.bmsDeviceState.sheJi_MV = getHexIndex(Bytes2HexString, 10, 6);
                                this.bmsDeviceState.isHoting = "01".equals(Bytes2HexString.substring(16, 18));
                                this.bmsDeviceState.junhengStatesShow = "01".equals(Bytes2HexString.substring(18, 20));
                                this.bmsDeviceState.junhengStates = "01".equals(Bytes2HexString.substring(20, 22));
                                if (System.currentTimeMillis() - this.lastToastShow > 5000) {
                                    this.lastToastShow = System.currentTimeMillis();
                                }
                            }
                            BmsBleCallback bmsBleCallback6 = this.bmsBleCallback;
                            if (bmsBleCallback6 != null) {
                                bmsBleCallback6.paramsBack();
                            }
                        }
                    } else if ((Bytes2HexString.startsWith("CCE") || Bytes2HexString.startsWith("CCD") || Bytes2HexString.startsWith("CCB")) && (bmsBleCallback = this.bmsBleCallback) != null) {
                        bmsBleCallback.sendSettingBack("01".equals(Bytes2HexString.substring(4, 6)), Bytes2HexString.substring(2, 4), Bytes2HexString.substring(4, 6));
                    }
                }
                if (Bytes2HexString.length() != 40) {
                    Log.e(TAG, "数据异常：" + Bytes2HexString);
                    return;
                }
                if (Bytes2HexString.startsWith("CCF5")) {
                    this.bmsDeviceState.singleGuoCH_MV = getHexIndex(Bytes2HexString, 4, 4);
                    this.bmsDeviceState.singleGuoCHRecover_MV = getHexIndex(Bytes2HexString, 8, 4);
                    this.bmsDeviceState.singleGuoF_MV = getHexIndex(Bytes2HexString, 12, 4);
                    this.bmsDeviceState.singleGuoFRecover_MV = getHexIndex(Bytes2HexString, 16, 4);
                    this.bmsDeviceState.sheJi_MAH = getHexIndex(Bytes2HexString, 20, 6);
                    this.bmsDeviceState.full_MAH = getHexIndex(Bytes2HexString, 26, 6);
                } else if (Bytes2HexString.startsWith("CCF6")) {
                    this.bmsDeviceState.groupGuoCH_MV = getHexIndex(Bytes2HexString, 4, 6);
                    this.bmsDeviceState.groupGuoCHRecover_MV = getHexIndex(Bytes2HexString, 10, 6);
                    this.bmsDeviceState.CHDHigh = getHexIndex(Bytes2HexString, 16, 4, true);
                    if (this.bmsDeviceState.CHDHigh < 1) {
                        this.bmsDeviceState.CHDHigh = 1;
                    }
                    this.bmsDeviceState.CHDHighRecover = getHexIndex(Bytes2HexString, 20, 4, true);
                    this.bmsDeviceState.CHDLow = getHexIndex(Bytes2HexString, 24, 4, true);
                    this.bmsDeviceState.CHDLowRecover = getHexIndex(Bytes2HexString, 28, 4, true);
                } else if (Bytes2HexString.startsWith("CCF7")) {
                    this.bmsDeviceState.groupGuoF_MV = getHexIndex(Bytes2HexString, 4, 6);
                    this.bmsDeviceState.groupGuoFRecover_MV = getHexIndex(Bytes2HexString, 10, 6);
                    this.bmsDeviceState.FDHigh = getHexIndex(Bytes2HexString, 16, 4, true);
                    if (this.bmsDeviceState.FDHigh < 1) {
                        this.bmsDeviceState.FDHigh = 1;
                    }
                    this.bmsDeviceState.FDHighRecover = getHexIndex(Bytes2HexString, 20, 4, true);
                    this.bmsDeviceState.FDLow = getHexIndex(Bytes2HexString, 24, 4, true);
                    this.bmsDeviceState.FDLowRecover = getHexIndex(Bytes2HexString, 28, 4, true);
                } else if (Bytes2HexString.startsWith("CCF8")) {
                    this.bmsDeviceState.sheJi_MV = getHexIndex(Bytes2HexString, 4, 6);
                    this.bmsDeviceState.zero_MV = getHexIndex(Bytes2HexString, 10, 6);
                    this.bmsDeviceState.full_MV = getHexIndex(Bytes2HexString, 16, 6);
                    this.bmsDeviceState.full_MA = getHexIndex(Bytes2HexString, 22, 6);
                } else if (Bytes2HexString.startsWith("CCFA")) {
                    this.bmsDeviceState.junhengOpenV = getHexIndex(Bytes2HexString, 4, 4);
                    this.bmsDeviceState.junhengOpenChaV = getHexIndex(Bytes2HexString, 8, 4);
                    this.bmsDeviceState.powerMode = getHexIndex(Bytes2HexString, 12, 4);
                }
                BmsBleCallback bmsBleCallback7 = this.bmsBleCallback;
                if (bmsBleCallback7 != null) {
                    bmsBleCallback7.settingBack();
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        Log.d(TAG, "接收到了数据3<<<<<<<<<<<<<<<<<<<<<<<" + Bytes2HexString);
    }
