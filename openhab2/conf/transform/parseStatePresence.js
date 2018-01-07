(function(i) {
    var array = i.split("&");

    if(array[2]=="presence=A") {
        return "ABSENT";
    }
    else {
        return "PRESENT";
    }    

})(input)