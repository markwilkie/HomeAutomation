angular.module('sensorWebApp')
    .factory('dataFactory', ['$http', function ($http) {

        var urlBase = 'http://sensors.cloudapp.net/Event/EventList/M/6';
        var dataFactory = {};

        dataFactory.getEvents = function () {
            return $http.get(urlBase);
        };

        dataFactory.getEvent = function (id) {
            return $http.get(urlBase + '/' + id);
        };

        return dataFactory;
    }]);