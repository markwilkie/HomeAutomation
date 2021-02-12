angular.module('sensorWebApp')
    .controller('eventsController', ['$scope', 'eventDataFactory',
        function ($scope, eventDataFactory) {

            $scope.events;

            getEvents();

            function getEvents() {
                eventDataFactory.getEvents()
                    .success(function (events) {
                        $scope.events = events;
                    })
                    .error(function (error) {
                        $scope.status = 'Unable to load event data: ' + error.message;
                    });
            }
        }]);