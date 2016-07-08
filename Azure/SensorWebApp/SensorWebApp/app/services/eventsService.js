angular.module('sensorWebApp')
    .factory('eventDataFactory', ['$http', function ($http) {

        var urlBase = 'http://wilkiehomeautomation.azurewebsites.net/api/events';
        var eventDataFactory = {};

        eventDataFactory.getEvents = function () {
            return $http.get(urlBase);
        };

        eventDataFactory.getEvent = function (id) {
            return $http.get(urlBase + '/' + id);
        };

        return eventDataFactory;
    }]);